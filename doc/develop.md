Useful tips for driver developers...

## Open issues

## Fixed issues

I sometimes see this in the log:

[00:21:23.906,494] [1;31m<err> i2c_nrfx_twim: Error 0x0BAE0001 occurred for message 0[0m

I slowed i2c down from 400kHz to 100kHz and the problem was still present:

[00:03:48.676,147] [1;31m<err> i2c_nrfx_twim: Error 0x0BAE0001 occurred for message 0[0m
[00:03:48.676,177] [1;31m<err> tps43: Touch data read error: -5[0m
[00:03:48.683,410] [1;31m<err> i2c_nrfx_twim: Error 0x0BAE0001 occurred for message 0[0m
[00:03:48.683,441] [1;31m<err> tps43: Touch data read error: -5[0m
[00:03:48.697,235] [0m<inf> tps43: Sending movement: dx=0, dy=1[0m
[00:03:48.703,369] [0m<inf> tps43: Touch state changed: up[0m
[00:03:48.704,010] [1;31m<err> i2c_nrfx_twim: Error 0x0BAE0001 occurred for message 0[0m
[00:03:48.704,040] [1;31m<err> tps43: Touch data read error: -5[0m
[00:03:51.975,036] [1;31m<err> i2c_nrfx_twim: Error 0x0BAE0001 occurred for message 0[0m
[00:03:51.975,067] [1;31m<err> tps43: Touch data read error: -5[0m
[00:03:59.645,477] [1;31m<err> i2c_nrfx_twim: Error 0x0BAE0001 occurred for message 0[0m

root cause seems to be incorrectly applying sleep wake code while zmk is in idle state.  fixed.

## Default register values

[00:00:01.020,904] [0m<inf> tps43: Dumping TPS43 registers[0m
[00:00:01.021,148] [0m<inf> tps43: SYSTEM_INFO_0 (0x000F): 0xD2[0m
[00:00:01.021,392] [0m<inf> tps43: SYSTEM_INFO_1 (0x0010): 0x00[0m
[00:00:01.021,667] [0m<inf> tps43: SYSTEM_CONTROL_0 (0x0431): 0xA0[0m
[00:00:01.021,911] [0m<inf> tps43: SYSTEM_CONTROL_1 (0x0432): 0x00[0m
[00:00:01.022,155] [0m<inf> tps43: SYSTEM_CONFIG_0 (0x058E): 0x64[0m
[00:00:01.022,430] [0m<inf> tps43: SYSTEM_CONFIG_1 (0x058F): 0x47[0m
[00:00:01.022,674] [0m<inf> tps43: GLOBAL_ATI_C (0x056B): 0x01[0m
[00:00:01.022,949] [0m<inf> tps43: ATI_TARGET (0x056D): 0x02BC[0m
[00:00:01.023,223] [0m<inf> tps43: REF_DRIFT_LIMIT (0x0571): 0x4B[0m
[00:00:01.023,468] [0m<inf> tps43: REATI_LOWER_LIMIT (0x0573): 0x00[0m
[00:00:01.023,742] [0m<inf> tps43: REATI_UPPER_LIMIT (0x0574): 0xFF[0m
[00:00:01.024,017] [0m<inf> tps43: MAX_COUNT_LIMIT (0x0575): 0x07D0[0m
[00:00:01.024,261] [0m<inf> tps43: ATI_RETRY_TIME (0x0577): 0x05[0m
[00:00:01.024,536] [0m<inf> tps43: REPORT_RATE_ACTIVE (0x057A): 0x000D[0m
[00:00:01.024,810] [0m<inf> tps43: REPORT_RATE_IDLE_TOUCH (0x057C): 0x0032[0m
[00:00:01.025,085] [0m<inf> tps43: REPORT_RATE_IDLE (0x057E): 0x0032[0m
[00:00:01.025,390] [0m<inf> tps43: REPORT_RATE_LP1 (0x0580): 0x0050[0m
[00:00:01.025,665] [0m<inf> tps43: REPORT_RATE_LP2 (0x0582): 0x00A0[0m
[00:00:01.025,909] [0m<inf> tps43: TIMEOUT_ACTIVE (0x0584): 0x05[0m
[00:00:01.026,153] [0m<inf> tps43: TIMEOUT_IDLE_TOUCH (0x0585): 0x3C[0m
[00:00:01.026,428] [0m<inf> tps43: TIMEOUT_IDLE (0x0586): 0x0A[0m
[00:00:01.026,672] [0m<inf> tps43: TIMEOUT_LP1 (0x0587): 0x01[0m
[00:00:01.026,947] [0m<inf> tps43: REF_UPDATE_TIME (0x0588): 0x08[0m
[00:00:01.027,191] [0m<inf> tps43: XY_STATIC_BETA (0x0633): 0x80[0m
[00:00:01.027,435] [0m<inf> tps43: ALP_COUNT_BETA (0x0634): 0x32[0m
[00:00:01.027,709] [0m<inf> tps43: ALP1_LTA_BETA (0x0635): 0x08[0m
[00:00:01.027,954] [0m<inf> tps43: ALP2_LTA_BETA (0x0636): 0x06[0m
[00:00:01.028,198] [0m<inf> tps43: XY_DYNAMIC_FILTER_BOTTOM (0x0637): 0x07[0m
[00:00:01.028,472] [0m<inf> tps43: XY_DYNAMIC_FILTER_LOWER (0x0638): 0x06[0m
[00:00:01.028,747] [0m<inf> tps43: XY_DYNAMIC_FILTER_UPPER (0x0639): 0x00FA[0m
[00:00:01.029,022] [0m<inf> tps43: X_RESOLUTION (0x066E): 0x0400[0m
[00:00:01.029,296] [0m<inf> tps43: Y_RESOLUTION (0x0670): 0x0400[0m
[00:00:01.029,571] [0m<inf> tps43: TAP_TIME (0x06B9): 150 ms[0m
[00:00:01.029,846] [0m<inf> tps43: TAP_DISTANCE (0x06BB): 25 px[0m
[00:00:01.030,151] [0m<inf> tps43: HOLD_TIME (0x06BD): 300 ms[0m
[00:00:01.030,426] [0m<inf> tps43: SWIPE_INITIAL_TIME (0x06BF): 150 ms[0m
[00:00:01.030,700] [0m<inf> tps43: SWIPE_INITIAL_DISTANCE (0x06C1): 300 px[0m
[00:00:01.030,975] [0m<inf> tps43: SWIPE_CONSECUTIVE_TIME (0x06C3): 0 ms[0m
[00:00:01.031,250] [0m<inf> tps43: SWIPE_CONSECUTIVE_DISTANCE (0x06C5): 2000 px[0m
[00:00:01.031,524] [0m<inf> tps43: SWIPE_ANGLE (0x06C7): 0x17[0m
[00:00:01.031,799] [0m<inf> tps43: SCROLL_INITIAL_DISTANCE (0x06C8): 50 px[0m
[00:00:01.032,043] [0m<inf> tps43: SCROLL_ANGLE (0x06CA): 0x25[0m
[00:00:01.032,318] [0m<inf> tps43: ZOOM_INITIAL_DISTANCE (0x06CB): 50 px[0m
[00:00:01.032,592] [0m<inf> tps43: ZOOM_CONSECUTIVE_DISTANCE (0x06CD): 25 px[0m