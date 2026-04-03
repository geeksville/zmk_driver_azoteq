#include <stdint.h>
#define DT_DRV_COMPAT azoteq_tps43

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <errno.h>

#include "tps43.h"

LOG_MODULE_REGISTER(tps43, CONFIG_INPUT_LOG_LEVEL);
 
/**
 * @brief Ends communication window with trackpad
 * 
 * After each read of trackpad registers, it is necessary to end the communication window
 * by writing the special address 0xEEEE, which causes a NACK from the device.
 * This is a mandatory step according to the IQS5xx protocol.
 * 
 * @param dev Pointer to trackpad device
 */
static void tps43_end_communication_window(const struct device *dev) {
    const struct tps43_config *config = dev->config;
    uint8_t end_buf[2];

    sys_put_be16(TPS43_REG_END_COMM_WINDOW, end_buf);

    int ret = i2c_write_dt(&config->i2c_bus, end_buf, sizeof(end_buf));
    if (ret != 0 && ret != -EIO) {
        LOG_INF("End communication window write returned: %d (NACK expected)", ret);
    }
}

/**
 * @brief Reads a sequence of trackpad registers
 * 
 * Performs reading of multiple bytes from sequential trackpad registers,
 * starting from the specified address. Used for reading related registers,
 * such as gesture events (GESTURE_EVENTS_0 and GESTURE_EVENTS_1).
 * 
 * @param dev Pointer to trackpad device
 * @param reg Starting register address (16-bit)
 * @param val Pointer to buffer for data
 * @param len Number of bytes to read
 * @return 0 on success, negative error code on failure
 */
static int read_sequence_registers(const struct device *dev, uint16_t reg, void *val, size_t len) {
    const struct tps43_config *config = dev->config;
    uint8_t addr_buf[2];
    addr_buf[0] = (uint8_t)((reg >> 8) & 0xFF);
    addr_buf[1] = (uint8_t)(reg & 0xFF);

    return i2c_write_read_dt(&config->i2c_bus, addr_buf, 2, val, len);
}

/**
 * @brief Reads a 16-bit trackpad register via I2C
 * 
 * Performs reading of a 16-bit value from the specified trackpad register.
 * Data is interpreted as big-endian (MSB first).
 * 
 * @param dev Pointer to trackpad device
 * @param reg Register address (16-bit)
 * @param val Pointer to variable to store the read value
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_read_reg16(const struct device *dev, uint16_t reg, uint16_t *val)
{
    const struct tps43_config *config = dev->config;
    uint8_t buf[2];
    // forms 2-byte register address: (MSB, LSB)
    // MSB: shift right by 8 bits (0x2F00 -> 0x2F)
    // LSB: bitwise AND with mask - mask leaves only lower byte (0x2F00 -> 0x00)
    uint8_t reg_buf[2] = {reg >> 8, reg & 0xFF};
    int ret;
    
    // writes register address (reg_buf) and reads 2 bytes of data (into buffer buf)
    ret = i2c_write_read_dt(&config->i2c_bus, reg_buf, sizeof(reg_buf), buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Register 0x%04x read error: %d", reg, ret);
        return ret;
    }
    
    // converts big-endian data (MSB first) back to 16-bit value
    *val = (buf[0] << 8) | buf[1];
    return 0;
}

/**
 * @brief Writes a 16-bit value to trackpad register via I2C
 * 
 * Performs writing of a 16-bit value to the specified trackpad register.
 * Data is transmitted as big-endian (MSB first).
 * 
 * @param dev Pointer to trackpad device
 * @param reg Register address (16-bit)
 * @param val Value to write (16-bit)
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_write_reg16(const struct device *dev, uint16_t reg, uint16_t val)
{
    const struct tps43_config *config = dev->config;
    // forms 4-byte register address: (MSB, LSB, MSB_VALUE, LSB_VALUE)
    uint8_t buf[4] = {reg >> 8, reg & 0xFF, val >> 8, val & 0xFF};
    int ret;
    
    ret = i2c_write_dt(&config->i2c_bus, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Register 0x%04x write error: %d", reg, ret);
        return ret;
    }
    
    return 0;
}

/**
 * @brief Reads an 8-bit trackpad register via I2C
 * 
 * Performs reading of an 8-bit value from the specified trackpad register.
 * Used for reading most configuration and status registers.
 * 
 * @param dev Pointer to trackpad device
 * @param reg Register address (16-bit)
 * @param val Pointer to variable to store the read value
 * @param with_err Flag to log error or expected behavior
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_read_reg8_w_err(const struct device *dev, uint16_t reg, uint8_t *val, bool with_err)
{
    const struct tps43_config *config = dev->config;
    // forms 2-byte register address: (MSB, LSB)
    uint8_t reg_buf[2] = {reg >> 8, reg & 0xFF};
    int ret;
    
    ret = i2c_write_read_dt(&config->i2c_bus, reg_buf, sizeof(reg_buf), val, 1);
    if (ret != 0) {
        if (!with_err) {
            LOG_INF("Expected completion of register 0x%04x read: %d", reg, ret);
        } else {
            LOG_ERR("Register 0x%04x read error: %d", reg, ret);
        }
        return ret;
    }
}

static inline int tps43_i2c_read_reg8(const struct device *dev, uint16_t reg, uint8_t *val)
{
    return tps43_i2c_read_reg8_w_err(dev, reg, val, true);
}

/**
 * @brief Writes an 8-bit value to trackpad register via I2C
 * 
 * Performs writing of an 8-bit value to the specified trackpad register.
 * Used for writing configuration and control registers.
 * 
 * @param dev Pointer to trackpad device
 * @param reg Register address (16-bit)
 * @param val Value to write (8-bit)
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_write_reg8(const struct device *dev, uint16_t reg, uint8_t val)
{
    const struct tps43_config *config = dev->config;
    uint8_t buf[3] = {reg >> 8, reg & 0xFF, val};
    int ret;
    
    ret = i2c_write_dt(&config->i2c_bus, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Register 0x%04x write error: %d", reg, ret);
        return ret;
    }
    
    return 0;
}

/**
 * @brief Callback handler for RDY pin interrupt from trackpad
 * 
 * Called when the RDY (Ready) pin state of the trackpad changes,
 * signaling that new data is available for reading.
 * Schedules execution of work handler to read the data.
 * 
 * @param dev Pointer to trackpad device
 * @param cb Pointer to GPIO callback structure
 * @param pins Mask of pins that triggered the interrupt
 */
 static void tps43_rdy_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
     struct tps43_drv_data *drv_data = CONTAINER_OF(cb, struct tps43_drv_data, rdy_cb);
 
     k_work_submit(&drv_data->work);
 }
 
/**
 * @brief Internal function to put trackpad into suspend/resume mode
 * 
 * Controls the SYSTEM_CONTROL_1 register (0x0432), setting or clearing the SUSPEND bit.
 * In suspend mode, the trackpad enters a low power consumption state and does not process
 * touches until wake-up.
 * 
 * @param dev Pointer to trackpad device
 * @param suspend true - enter suspend, false - exit suspend
 * @param lock_held true if semaphore is already held (for internal use)
 * @return 0 on success, negative error code on failure
 */
static int tps43_set_suspend_internal(const struct device *dev, bool suspend, bool lock_held) {
    struct tps43_drv_data *drv_data = dev->data;
    const struct tps43_config *config = dev->config;
    int ret = 0;

    // If power management is disabled, do nothing
    if (!config->enable_power_management) {
        return 0;
    }

    // Acquire semaphore if not already held
    if (!lock_held) {
        if (k_sem_take(&drv_data->lock, K_MSEC(100)) != 0) {
            LOG_WRN("Failed to acquire semaphore for suspend/resume");
            return -EBUSY;
        }
    }

    // Disable RDY interrupts when entering suspend (before any I2C operations)
    // This prevents race condition when RDY fires between suspend attempt and flag setting
    if (suspend && config->rdy_gpio.port != NULL) {
        ret = gpio_pin_interrupt_configure_dt(&config->rdy_gpio, GPIO_INT_DISABLE);
        if (ret == 0) {
            LOG_INF("RDY interrupts disabled before suspend");
        }
    }

    uint8_t control_reg = 0;
    
    // When exiting suspend, first transaction will return NACK (section 7.3.1)
    if (drv_data->suspended && !suspend) {
        ret = tps43_i2c_read_reg8_w_err(dev, TPS43_REG_SYSTEM_CONTROL_1, &control_reg, false);
        k_sleep(K_MSEC(200));
        LOG_INF("I2C Wake: device awakened from suspend");
        
        // After wake-up, read register again
        ret = tps43_i2c_read_reg8_w_err(dev, TPS43_REG_SYSTEM_CONTROL_1, &control_reg, false);

    } else if (!drv_data->suspended) {
        // Read current value only if not in suspend
        ret = tps43_i2c_read_reg8_w_err(dev, TPS43_REG_SYSTEM_CONTROL_1, &control_reg, false);
        if (ret != 0) {
            // If error -5 (EIO) when trying to enter suspend - device already in suspend
            if (ret == -EIO && suspend) {
                LOG_INF("Device already in suspend (I2C error)");
                drv_data->suspended = true;
                ret = 0;
                goto done;
            }
            LOG_ERR("SYSTEM_CONTROL_1 read error: %d", ret);
            goto done;
        }
    }

    if (suspend) {
        control_reg |= TPS43_SUSPEND;
        LOG_INF("Entering suspend (low power consumption)");
    } else {
        control_reg &= ~TPS43_SUSPEND;
        LOG_INF("Exiting suspend");
    }

    ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONTROL_1, control_reg);
    if (ret != 0) {
        if (ret == -EIO && suspend) {
            LOG_INF("Failed to write suspend, device already in suspend");
            drv_data->suspended = true;
            ret = 0;
            goto done;
        }
        LOG_ERR("SYSTEM_CONTROL_1 write error: %d", ret);
        goto done;
    }

    drv_data->suspended = suspend;

done:
    // Enable RDY interrupts after resume
    if (!suspend && config->rdy_gpio.port != NULL) {
        ret = gpio_pin_interrupt_configure_dt(&config->rdy_gpio, GPIO_INT_EDGE_TO_ACTIVE);
        if (ret == 0) {
            LOG_INF("RDY interrupts enabled");
        }
    }
    tps43_end_communication_window(dev);
    if (!lock_held) {
        k_sem_give(&drv_data->lock);
    }
    return ret;
}

/**
 * @brief Main work handler for processing trackpad events
 * 
 * Executed when receiving an interrupt from the trackpad (RDY pin).
 * Reads and processes gesture events, cursor movement and scrolling,
 * converting them into input events for the ZMK system.
 * Also manages trackpad wake-up from suspend mode when activity is detected.
 * 
 * Protected by semaphore to prevent interruption by other I2C operations,
 * which ensures smooth cursor movement without interruptions.
 * 
 * @param work Pointer to work structure
 */
static void tps43_work_handler(struct k_work *work) {
    struct tps43_drv_data *drv_data = CONTAINER_OF(work, struct tps43_drv_data, work);
    const struct device *dev = drv_data->dev;
    const struct tps43_config *config = dev->config;
    bool is_scroll_active = drv_data->scroll_active;
    bool is_drag_active = drv_data->drag_active;
    int ret;
    
    // If device is in suspend, ignore interrupt (RDY should be disabled)
    if (drv_data->suspended) {
        LOG_WRN("RDY interrupt in suspend mode - ignoring");
        return;
    }
    
    // Acquire semaphore to protect all I2C operations from interruption
    // This prevents conflicts during simultaneous trackpad access
    k_sem_take(&drv_data->lock, K_FOREVER);

    uint8_t sys_info = 0;
    ret = tps43_i2c_read_reg8(dev, TPS43_REG_SYSTEM_INFO_1, &sys_info);
    if (ret < 0) {
        LOG_ERR("System information read error: %d", ret);
        goto done;
    }

    uint8_t gestures_events[2];
    ret = read_sequence_registers(dev, TPS43_REG_GESTURE_EVENTS_0, &gestures_events, 2);
    if (ret < 0) {
        LOG_ERR("Gesture events read error: %d", ret);
        goto done;
    }
    
    if (gestures_events[0] != 0 || gestures_events[1] != 0) {

        LOG_INF("Gestures: Single=0x%02X, Multi=0x%02X", gestures_events[0], gestures_events[1]);

        if (gestures_events[0] & TPS43_SINGLE_TAP) {
            LOG_INF("Single tap → LEFT BUTTON");
            input_report_key(dev, INPUT_BTN_0, 1, true, K_FOREVER);
            input_report_key(dev, INPUT_BTN_0, 0, true, K_FOREVER);  
        }
        if (gestures_events[1] & TPS43_TWO_FINGER_TAP) {
            LOG_INF("Two finger tap → RIGHT BUTTON");
            input_report_key(dev, INPUT_BTN_1, 1, true, K_FOREVER);  
            input_report_key(dev, INPUT_BTN_1, 0, true, K_FOREVER); 
        }
        if ((gestures_events[0] & TPS43_PRESS_AND_HOLD) && (!(is_drag_active))) {
            LOG_INF("Press and hold detected - DRAG (HOLD LEFT BUTTON)");
            // set internal drag flag and press left mouse button
            is_drag_active = true;
            input_report_key(dev, INPUT_BTN_0, 1, true, K_FOREVER); 
        }
        if ((!(gestures_events[0] & TPS43_PRESS_AND_HOLD)) && (is_drag_active)) {
            LOG_INF("Press and hold end detected - RELEASE (RELEASE LEFT BUTTON)");
            // release drag flag and release left mouse button
            is_drag_active = false;
            input_report_key(dev, INPUT_BTN_0, 0, true, K_FOREVER);   // release + sync
        }
        if (gestures_events[1] & TPS43_SCROLL) {
            LOG_INF("Scroll detected - Scrolling");
            // set scroll flag for processing in tp_movement block
            is_scroll_active = true;
        }
    }

    if (sys_info & TPS43_TP_MOVEMENT) {
        int16_t rel_x = 0, rel_y = 0;
        ret = tps43_i2c_read_reg16(dev, TPS43_REG_REL_X, (uint16_t*)&rel_x);
        if (ret < 0) {
            LOG_ERR("REL_X read error: %d", ret);
            goto done;
        }
        ret = tps43_i2c_read_reg16(dev, TPS43_REG_REL_Y, (uint16_t*)&rel_y);
        if (ret < 0) {
            LOG_ERR("REL_Y read error: %d", ret);
            goto done;
        }
        // Send cursor movement
        if (rel_x != 0 || rel_y != 0) {
            if (rel_x != 0 ) {
                int32_t scaled_x = ((int32_t)rel_x * config->sensitivity) / 100;
                rel_x = (int16_t)CLAMP(scaled_x, INT16_MIN, INT16_MAX);
            }
            if (rel_y != 0) { 
                int32_t scaled_y = ((int32_t)rel_y * config->sensitivity) / 100;
                rel_y = (int16_t)CLAMP(scaled_y, INT16_MIN, INT16_MAX);
            }
            LOG_INF("Sending movement: dx=%d, dy=%d", rel_x, rel_y);

            // Handle three-finger swipes
            if (config->swipes) {
                uint8_t num_fingers = 0;
                ret = tps43_i2c_read_reg8(dev, TPS43_REG_NUM_FINGERS, &num_fingers);
                if (ret < 0) {
                    LOG_ERR("NUM_FINGERS read error: %d", ret);
                    goto done;
                }
                if (num_fingers == 3) {
                    if (rel_x < 0) {
                        LOG_INF("3-finger swipe left - mouse button 6");
                        input_report_key(dev, INPUT_BTN_6, 1, true, K_FOREVER);
                        input_report_key(dev, INPUT_BTN_6, 0, true, K_FOREVER);
                    }
                    if (rel_x > 0) {
                        LOG_INF("3-finger swipe right - mouse button 7");
                        input_report_key(dev, INPUT_BTN_7, 1, true, K_FOREVER);
                        input_report_key(dev, INPUT_BTN_7, 0, true, K_FOREVER);
                    }
                }
            }

        
            if (is_scroll_active) {
                // Scroll processing: keep only dominant axis
                if (abs(rel_x) > abs(rel_y)) {
                    // Horizontal scroll
                    if (config->invert_scroll_x) {
                        rel_x = -rel_x;
                    }
                    int16_t wheel = (rel_x * config->scroll_sensitivity) / 100;
                    input_report_rel(dev, INPUT_REL_HWHEEL, wheel, true, K_FOREVER);
                } else {
                    // Vertical scroll
                    if (config->invert_scroll_y) {
                        rel_y = -rel_y;
                    }
                    int16_t wheel = (rel_y * config->scroll_sensitivity) / 100;
                    input_report_rel(dev, INPUT_REL_WHEEL, wheel, true, K_FOREVER);
                }
                is_scroll_active = false;
            } else {
                // Normal cursor movement
                input_report_rel(dev, INPUT_REL_X, rel_x, false, K_FOREVER);
                input_report_rel(dev, INPUT_REL_Y, rel_y, true, K_FOREVER);
            }
        }
    }

done:
    // Save for next call
    drv_data->scroll_active = is_scroll_active;
    drv_data->drag_active = is_drag_active;
    tps43_end_communication_window(dev);
    
    // Release semaphore after completing all I2C operations
    k_sem_give(&drv_data->lock);
}

/**
 * @brief Resets driver internal state values
 * 
 * Initializes all driver state flags to initial values.
 * Used during initialization and device reset.
 * 
 * @param dev Pointer to trackpad device
 * @return 0 on success
 */
static int tps43_reset_values(const struct device *dev) {
    struct tps43_drv_data *drv_data = dev->data;

    drv_data->device_ready = false;
    drv_data->initialized = false;
    drv_data->scroll_active = false;
    drv_data->drag_active = false;

    LOG_INF("Values reset");
    return 0;
}

/**
 * @brief Configures trackpad system registers for operation
 * 
 * Sets up trackpad registers to track touch events, gestures and movement.
 * Enables necessary gestures (single tap, press and hold, scroll, two finger tap),
 * configures axis inversion and sets setup complete flag.
 * 
 * @param dev Pointer to trackpad device
 * @return 0 on success, negative error code on failure
 */
static int tps43_configure_device(const struct device *dev) {

    const struct tps43_config *config = dev->config;
    int ret;

    // write to TPS43_REG_SYSTEM_CONFIG_1 events to track  
    uint8_t events_to_track = TPS43_TP_EVENT | TPS43_EVENT_MODE;
    
    // Gestures (single_tap, press_and_hold, scroll, two_finger_tap)
    if (config->single_tap || config->press_and_hold || 
        config->scroll || config->two_finger_tap) {
        events_to_track |= TPS43_GESTURE_EVENT;
    }
    
    // Touch events for absolute coordinates
    events_to_track |= TPS43_TOUCH_EVENT;
    
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONFIG_1, events_to_track);
    if (ret != 0) {
        LOG_WRN("Events to track write error: %d", ret);
        return ret;
    }
    LOG_INF("Events configured: 0x%02X", events_to_track);

    // axis configuration
    uint8_t xy_config = 0;
    xy_config |= config->invert_x ? TPS43_FLIP_X : 0;
    xy_config |= config->invert_y ? TPS43_FLIP_Y : 0;
    xy_config |= config->switch_xy ? TPS43_SWITCH_XY_AXIS : 0;
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_XY_CONFIG_0, xy_config);
    if (ret != 0) {
        LOG_WRN("XY configuration write error: %d", ret);
        return ret;
    }

    // enable single gestures at hardware level
    if (config->single_tap || config->press_and_hold || config->swipes) {
        uint8_t single_gestures = 0;
        single_gestures |= config->single_tap ? TPS43_SINGLE_TAP : 0;
        single_gestures |= config->press_and_hold ? TPS43_PRESS_AND_HOLD : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_UP : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_DOWN : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_LEFT : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_RIGHT : 0;
        
        ret = tps43_i2c_write_reg8(dev, TPS43_REG_SINGLE_FINGER_GESTURES, single_gestures);
        if (ret != 0) {
            LOG_WRN("Single gestures configuration error: %d", ret);
            return ret;
        }
        LOG_INF("Single gestures enabled: 0x%02X", single_gestures);
    }

    // enable multi-gestures
    if (config->two_finger_tap || config->scroll) {
        uint8_t multi_gestures = 0;
        multi_gestures |= config->two_finger_tap ? TPS43_TWO_FINGER_TAP : 0;
        multi_gestures |= config->scroll ? TPS43_SCROLL : 0;
        
        ret = tps43_i2c_write_reg8(dev, TPS43_REG_MULTI_FINGER_GESTURES, multi_gestures);
        if (ret != 0) {
            LOG_WRN("Multi-gesture configuration error: %d", ret);
            return ret;
        }
        LOG_INF("Multi-gestures enabled: 0x%02X", multi_gestures);
    }

    // filter configuration
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_FILTER_SETTINGS, config->filter_settings);
    if (ret != 0) {
        LOG_WRN("Filter settings write error: %d", ret);
        return ret;
    }
    LOG_INF("Filter settings set: 0x%02X", config->filter_settings);

    // set configuration complete flag
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONFIG_0, TPS43_SETUP_COMPLETE);
    if (ret != 0) {
        LOG_WRN("Setup complete flag write error: %d", ret);
        return ret;
    }

    return 0;
}

/**
 * @brief Checks device reset state and performs reconfiguration
 * 
 * Waits for device readiness after reset, checks SHOW_RESET flag
 * and sends reset acknowledgment (ACK_RESET) when necessary.
 * Then performs full device configuration.
 * 
 * @param dev Pointer to trackpad device
 * @return 0 on success, negative error code on failure
 */
static int check_reset_and_reconfigure(const struct device *dev) {
    struct tps43_drv_data *drv_data = dev->data;
    int ret;
    uint8_t sys_info = 0;
    uint8_t wait_count = 0;
    const uint8_t max_wait_count = 50;

    // Wait for device readiness
    do {
        ret = tps43_i2c_read_reg8(dev, TPS43_REG_SYSTEM_INFO_0, &sys_info);
        if (ret < 0) {
            k_sleep(K_MSEC(100));
            wait_count++;
            if (wait_count >= max_wait_count) {
                LOG_ERR("Device not responding after %d ms", wait_count * 100);
                return -ETIMEDOUT;
            }
        }
    } while (ret < 0);
    
    LOG_INF("Device ready after %d ms", wait_count * 100);

    // after reset, set flag to acknowledge that reset was performed
    if (sys_info & TPS43_SHOW_RESET) {
        LOG_INF("SHOW_RESET detected, sending ACK_RESET");
        ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONTROL_0, TPS43_ACK_RESET);
        if (ret != 0) {
            LOG_ERR("ACK_RESET send error: %d", ret);
            return ret;
        }
        k_sleep(K_MSEC(10));
    }

    ret = tps43_configure_device(dev);
    if (ret != 0) {
        LOG_ERR("Device configuration error: %d", ret);
        return ret;
    }

    drv_data->device_ready = true;
    
    return 0;
}

/**
 * @brief Public function to put trackpad into suspend/resume
 * 
 * @param dev Pointer to trackpad device
 * @param suspend true - enter suspend, false - exit suspend
 * @return 0 on success, negative error code on failure
 */
static int tps43_set_suspend(const struct device *dev, bool suspend) {
    return tps43_set_suspend_internal(dev, suspend, false);
}

/**
 * @brief Initializes TPS43 trackpad driver
 * 
 * Performs full driver initialization: checks I2C bus availability,
 * performs hardware reset via GPIO RST (if connected), waits for device
 * readiness, configures trackpad registers and sets up GPIO RDY interrupts.
 * Also initializes power management system when necessary.
 * 
 * @param dev Pointer to trackpad device
 * @return 0 on success, negative error code on failure
 */
static int tps43_init(const struct device *dev) {

    struct tps43_drv_data *drv_data = dev->data;
    const struct tps43_config *config = dev->config;
    int ret;

    drv_data->dev = dev;

    LOG_INF("=== Azoteq tps43 driver for device %s ===", dev->name);
    
    // Check I2C bus
    if (!device_is_ready(config->i2c_bus.bus)) {
        LOG_ERR("I2C bus not available");
        return -ENODEV;
    }
    
    LOG_INF("I2C bus: %s", config->i2c_bus.bus->name);
    LOG_INF("I2C address: 0x%02x", config->i2c_bus.addr);

    ret = tps43_reset_values(dev);
    if (ret != 0) {
        LOG_ERR("Values reset error: %d", ret);
        return ret;
    }

    // GPIO reset via hardware RST
    if (config->rst_gpio.port) {
        ret = gpio_pin_configure_dt(&config->rst_gpio, GPIO_OUTPUT_INACTIVE);
        if (ret != 0) {
            LOG_ERR("RST GPIO configuration error: %d", ret);
            return ret;
        }
        
        gpio_pin_set_dt(&config->rst_gpio, 0);
        k_sleep(K_MSEC(10));
        gpio_pin_set_dt(&config->rst_gpio, 1);
        k_sleep(K_MSEC(610));
        
        LOG_INF("Hardware reset completed");
    }

    // check SHOW_RESET and configure
    ret = check_reset_and_reconfigure(dev);
    if (ret != 0) {
        LOG_ERR("Device configuration error: %d", ret);
        return ret;
    }

    // configure RDY interrupts only AFTER device configuration!
    if (config->rdy_gpio.port != NULL) {
        ret = gpio_pin_configure_dt(&config->rdy_gpio, GPIO_INPUT);
        if (ret != 0) {
            LOG_WRN("RDY GPIO configuration error: %d", ret);
        } else {
            ret = gpio_pin_interrupt_configure_dt(&config->rdy_gpio, 
                                                    GPIO_INT_EDGE_TO_ACTIVE);
            if (ret == 0) {
                gpio_init_callback(&drv_data->rdy_cb, tps43_rdy_callback, 
                                    BIT(config->rdy_gpio.pin));
                ret = gpio_add_callback(config->rdy_gpio.port, &drv_data->rdy_cb);
                if (ret == 0) {
                    LOG_INF("RDY interrupt configured");
                } else {
                    LOG_WRN("RDY callback add error: %d", ret);
                }
            }
        }
    }

    drv_data->initialized = true;
    drv_data->suspended = false;

    // Initialize semaphore to protect I2C operations
    // First parameter - initial count (1 = available)
    // Second parameter - maximum count (1 = binary semaphore)
    k_sem_init(&drv_data->lock, 1, 1);

    k_work_init(&drv_data->work, tps43_work_handler);
    
    LOG_INF("TPS43 driver successfully initialized");
    return 0;
}

 
#define TPS43_INIT(inst)                                                                             \
    static struct tps43_drv_data tps43_##inst##_drvdata = {                                          \
        .device_ready = false,                                                                       \
        .initialized = false,                                                                        \
        .scroll_active = false,                                                                      \
        .drag_active = false,                                                                        \
        .suspended = false,                                                                          \
    };                                                                                               \
                                                                                                     \
    static const struct tps43_config tps43_##inst##_config = {                                       \
        .i2c_bus = I2C_DT_SPEC_INST_GET(inst),                                                       \
        .rdy_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, rdy_gpios, {0}),                                  \
        .rst_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, rst_gpios, {0}),                                  \
        .single_tap = DT_INST_PROP(inst, single_tap),                                                \
        .press_and_hold = DT_INST_PROP(inst, press_and_hold),                                        \
        .two_finger_tap = DT_INST_PROP(inst, two_finger_tap),                                        \
        .scroll = DT_INST_PROP(inst, scroll),                                                        \
        .swipes = DT_INST_PROP(inst, swipes),                                                        \
        .invert_x = DT_INST_PROP(inst, invert_x),                                                    \
        .invert_y = DT_INST_PROP(inst, invert_y),                                                    \
        .switch_xy = DT_INST_PROP(inst, switch_xy),                                                  \
        .invert_scroll_x = DT_INST_PROP(inst, invert_scroll_x),                                      \
        .invert_scroll_y = DT_INST_PROP(inst, invert_scroll_y),                                      \
        .sensitivity = DT_INST_PROP_OR(inst, sensitivity, 100),                                      \
        .scroll_sensitivity = DT_INST_PROP_OR(inst, scroll_sensitivity, 50),                         \
        .enable_power_management = DT_INST_PROP_OR(inst, enable_power_management, true),             \
        .filter_settings = DT_INST_PROP_OR(inst, filter_settings, 0x0F),                             \
    };                                                                                               \
                                                                                                     \
    DEVICE_DT_INST_DEFINE(inst, tps43_init, NULL, &tps43_##inst##_drvdata, &tps43_##inst##_config,   \
                        POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);                              \
    BUILD_ASSERT(DT_INST_REG_ADDR(inst) == TPS43_I2C_ADDR, "I2C address mismatch");


DT_INST_FOREACH_STATUS_OKAY(TPS43_INIT)

/**
 * @brief Public function to manage trackpad sleep mode
 * 
 * This function is used by ZMK power management system (via tps43_idle_sleeper)
 * to put trackpad into sleep mode when keyboard transitions to idle/sleep state.
 * 
 * @param dev Pointer to trackpad device
 * @param sleep true - enter sleep mode, false - wake up
 * @return 0 on success, negative error code on failure
 */
int tps43_set_sleep(const struct device *dev, bool sleep) {
    if (dev == NULL) {
        return -EINVAL;
    }
    return tps43_set_suspend(dev, sleep);
}
