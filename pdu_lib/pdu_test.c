#include "pdu.h"

#include <stdio.h>
#include <string.h>

static int expect_pdu(const char* name, const char* number, const char* text,
		      const unsigned char* expected, size_t expected_length)
{
	unsigned char actual[SMS_MAX_PDU_LENGTH];
	int actual_length = pdu_encode("", number, text, actual, sizeof(actual));

	if (actual_length != (int)expected_length ||
	    memcmp(actual, expected, expected_length) != 0) {
		fprintf(stderr, "%s: encoded PDU does not match\n", name);
		return 1;
	}
	return 0;
}

int main(void)
{
	static const unsigned char ascii_pdu[] = {
		0x00, 0x11, 0x00, 0x0A, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09,
		0x00, 0x00, 0xB0, 0x05, 0xE8, 0x32, 0x9B, 0xFD, 0x06,
	};
	static const unsigned char ucs2_pdu[] = {
		0x00, 0x11, 0x00, 0x0B, 0x91, 0x97, 0x50, 0x25, 0x00, 0x53,
		0xF3, 0x00, 0x08, 0xB0, 0x2C, 0x04, 0x14, 0x04, 0x38, 0x04,
		0x3C, 0x04, 0x30, 0x00, 0x2C, 0x00, 0x20, 0x04, 0x3F, 0x04,
		0x38, 0x04, 0x48, 0x04, 0x35, 0x04, 0x42, 0x00, 0x20, 0x04,
		0x42, 0x04, 0x35, 0x04, 0x31, 0x04, 0x35, 0x00, 0x20, 0x00,
		0x4E, 0x00, 0x4C, 0x00, 0x36, 0x00, 0x37, 0x00, 0x38,
	};
	unsigned char pdu[SMS_MAX_PDU_LENGTH];
	char long_ucs2[71 * 2 + 1];
	int failed = 0;

	failed |= expect_pdu("ASCII", "+1234567890", "hello",
			     ascii_pdu, sizeof(ascii_pdu));
	failed |= expect_pdu("UCS-2", "+79055200353",
			     "Дима, пишет тебе NL678",
			     ucs2_pdu, sizeof(ucs2_pdu));

	if (pdu_encode("", "+12x34", "test", pdu, sizeof(pdu)) >= 0) {
		fprintf(stderr, "invalid destination was accepted\n");
		failed = 1;
	}
	if (pdu_encode("", "+1234", "\xF0\x9F\x98\x80", pdu,
		       sizeof(pdu)) >= 0) {
		fprintf(stderr, "non-UCS-2 code point was accepted\n");
		failed = 1;
	}
	for (int i = 0; i < 71; ++i) {
		long_ucs2[2 * i] = (char)0xC2;
		long_ucs2[2 * i + 1] = (char)0xA3;
	}
	long_ucs2[sizeof(long_ucs2) - 1] = '\0';
	if (pdu_encode("", "+1234", long_ucs2, pdu, sizeof(pdu)) >= 0) {
		fprintf(stderr, "overlong UCS-2 message was accepted\n");
		failed = 1;
	}

	return failed;
}
