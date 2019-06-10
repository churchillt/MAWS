#! /bin/sh

### BEGIN INIT INFO
# Provides:             maws
# Should-Start:         $network
# Should-Stop:
# Default-Start:        3 4 5  
# Default-Stop:         0 1 2 6
# Short-Description:    Mobile Application Server
# Description:          Server for supporting WISA mobile application 
### END INIT INFO

DAEMON=/usr/sbin/maws

### start/stop maws 
case "$1" in
    start) 
	sleep 5
        start-stop-daemon --start --quiet --oknodo --exe /usr/sbin/maws -- -n den
	;;
    stop) 
   	start-stop-daemon --stop --quiet --oknodo --pidfile /run/maws.pid
 	;;
esac


	
