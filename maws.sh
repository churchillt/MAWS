#!//bin/bash

    case "$1" in
    'start')
    	{
	/usr/local/bin/maws -n den
	} & disown
	;;
    'stop')
    	killall maws
	;;
    esac


	
