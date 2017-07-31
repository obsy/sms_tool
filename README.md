SMS Tool for 3g modem
===================

* sms tool for various of 3g modem
* read sms as raw pdu or decoded text
* support ucs2 decoding

Usage:
----------------

	root@lede:~# sms_tool
	usage: [options] send phoneNumber message
	       [options] recv [raw]
	       [options] delete msg_index | all
	       [options] status
	options:
	       -d <tty device> (default /dev/ttyUSB0)
	       -b <baudrate> (default 115200)
	       -s <preferred storage>

