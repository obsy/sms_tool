SMS Tool for 3g modem
===================

* sms tool for various of 3g modem
* including OpenWRT LuCI sms viewer
* Tested on TP-LINK TL-MR10U
* read sms as raw pdu or decoded text
* support ucs2 decoding

Usage:
----------------

	root@AirStation_3g:~# ./sms_tool recv
	MSG: 0
	From:10086
	Date/Time:04/10/14 16:37:00
	SMS segment 3 of 3
	0140326-20140425。 中国移动

	MSG: 1
	From:10086
	Date/Time:04/10/14 16:37:00
	SMS segment 3 of 2
	K)，有效期20140326-20140425。"赠送200M本地流量": 剩余GPRS为200.00M(1M=1024K)，有效期2
	MSG: 2
	From:10086
	Date/Time:04/10/14 16:37:00
	SMS segment 3 of 1
	尊敬的客户，您好！截至04月11日00时，您的"上网套餐30元流量包（含专属叠加包）": 剩余GPRS为299.77M(1M=1024
	root@AirStation_3g:~#

	root@AirStation_3g:~# ./sms_tool
	usage: [options] send phoneNumber message
	       [options] recv [raw]> msg.txt
	       [options] status
	       [options] delete msg_index | all
	options:
	       -D /dev/ttyUSB1
	       -b 115200
	root@AirStation_3g:~#

