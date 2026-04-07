# A nasty little script to test sending ZOOMIN keys to see if apps understand them.  Needs apt install evemu-tools
KBD=/dev/input/event6
echo "Sleeping 5 seconds to let you switch to an app that can handle zoom events (e.g. a web browser)"
sleep 5
echo "Testing zoom in"
sudo evemu-event $KBD --type EV_KEY --code KEY_ZOOMIN --value 1 --sync
sudo evemu-event $KBD --type EV_KEY --code KEY_ZOOMIN --value 0 --sync