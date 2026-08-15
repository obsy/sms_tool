#include "pdu.h"

#include <stdio.h>
#include <stdlib.h>
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

static int gsm7_at(const unsigned char* user_data, int septet)
{
	const int bit_position = 49 + septet * 7;
	const int byte_position = bit_position / 8;
	const int shift = bit_position % 8;
	unsigned int value = user_data[byte_position] >> shift;
	if (shift > 1)
		value |= user_data[byte_position + 1] << (8 - shift);
	return value & 0x7F;
}

static int test_ucs2_multipart(void)
{
	unsigned char pdu[SMS_MAX_PDU_LENGTH];
	char text[71 * 2 + 1];
	int failed = 0;
	int total_parts;

	for (int i = 0; i < 71; ++i) {
		text[2 * i] = (char)0xC2;
		text[2 * i + 1] = (char)0xA3;
	}
	text[sizeof(text) - 1] = '\0';

	int length = pdu_encode_multipart("", "+1234567890", text, 0x42, 1,
					  &total_parts, pdu, sizeof(pdu));
	static const unsigned char first_header[] = {0x05, 0x00, 0x03, 0x42, 0x02, 0x01};
	if (length != 154 || total_parts != 2 || pdu[1] != 0x51 ||
	    pdu[11] != 0x08 || pdu[13] != 140 ||
	    memcmp(pdu + 14, first_header, sizeof(first_header)) != 0) {
		fprintf(stderr, "invalid first UCS-2 multipart PDU\n");
		failed = 1;
	}
	for (int i = 0; i < 67; ++i) {
		if (pdu[20 + 2 * i] != 0x00 || pdu[21 + 2 * i] != 0xA3) {
			fprintf(stderr, "invalid UCS-2 payload in first part\n");
			failed = 1;
			break;
		}
	}

	length = pdu_encode_multipart("", "+1234567890", text, 0x42, 2,
				      &total_parts, pdu, sizeof(pdu));
	static const unsigned char second_data[] = {
		0x05, 0x00, 0x03, 0x42, 0x02, 0x02,
		0x00, 0xA3, 0x00, 0xA3, 0x00, 0xA3, 0x00, 0xA3,
	};
	if (length != 28 || pdu[13] != sizeof(second_data) ||
	    memcmp(pdu + 14, second_data, sizeof(second_data)) != 0) {
		fprintf(stderr, "invalid second UCS-2 multipart PDU\n");
		failed = 1;
	}

	return failed;
}

static int test_gsm7_multipart(void)
{
	unsigned char pdu[SMS_MAX_PDU_LENGTH];
	char text[162];
	int failed = 0;
	int total_parts;

	memset(text, 'A', sizeof(text) - 1);
	text[sizeof(text) - 1] = '\0';
	int length = pdu_encode_multipart("", "+1234567890", text, 0x24, 1,
					  &total_parts, pdu, sizeof(pdu));
	static const unsigned char first_header[] = {0x05, 0x00, 0x03, 0x24, 0x02, 0x01};
	if (length != 154 || total_parts != 2 || pdu[1] != 0x51 ||
	    pdu[11] != 0x00 || pdu[13] != 160 ||
	    memcmp(pdu + 14, first_header, sizeof(first_header)) != 0) {
		fprintf(stderr, "invalid first GSM-7 multipart PDU\n");
		failed = 1;
	}
	for (int i = 0; i < 153; ++i) {
		if (gsm7_at(pdu + 14, i) != 'A') {
			fprintf(stderr, "invalid packed GSM-7 payload in first part\n");
			failed = 1;
			break;
		}
	}

	length = pdu_encode_multipart("", "+1234567890", text, 0x24, 2,
				      &total_parts, pdu, sizeof(pdu));
	if (length != 28 || pdu[13] != 15 || pdu[19] != 0x02) {
		fprintf(stderr, "invalid second GSM-7 multipart PDU\n");
		failed = 1;
	}
	for (int i = 0; i < 8; ++i) {
		if (gsm7_at(pdu + 14, i) != 'A') {
			fprintf(stderr, "invalid packed GSM-7 payload in second part\n");
			failed = 1;
			break;
		}
	}

	return failed;
}

static int test_gsm7_escape_boundary(void)
{
	unsigned char pdu[SMS_MAX_PDU_LENGTH];
	char text[161];
	int total_parts;

	memset(text, 'A', 152);
	text[152] = '{';
	memset(text + 153, 'B', 7);
	text[160] = '\0';

	int length = pdu_encode_multipart("", "+1234567890", text, 0x55, 1,
					  &total_parts, pdu, sizeof(pdu));
	if (length < 0 || total_parts != 2 || pdu[13] != 159)
		return 1;
	length = pdu_encode_multipart("", "+1234567890", text, 0x55, 2,
				      &total_parts, pdu, sizeof(pdu));
	if (length < 0 || gsm7_at(pdu + 14, 0) != 0x1B ||
	    gsm7_at(pdu + 14, 1) != 0x28) {
		fprintf(stderr, "GSM-7 extension pair was split between parts\n");
		return 1;
	}
	return 0;
}

static int test_part_count_limit(void)
{
	const size_t max_length = 153 * 255;
	char* text = malloc(max_length + 2);
	unsigned char pdu[SMS_MAX_PDU_LENGTH];
	int total_parts;
	int failed = 0;

	if (!text)
		return 1;
	memset(text, 'A', max_length + 1);
	text[max_length] = '\0';
	if (pdu_encode_multipart("", "+1234567890", text, 0x66, 255,
				 &total_parts, pdu, sizeof(pdu)) < 0 ||
	    total_parts != 255) {
		fprintf(stderr, "255-part message was rejected\n");
		failed = 1;
	}
	text[max_length] = 'A';
	text[max_length + 1] = '\0';
	if (pdu_encode_multipart("", "+1234567890", text, 0x66, 1,
				 &total_parts, pdu, sizeof(pdu)) >= 0) {
		fprintf(stderr, "256-part message was accepted\n");
		failed = 1;
	}
	free(text);
	return failed;
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
	failed |= test_ucs2_multipart();
	failed |= test_gsm7_multipart();
	failed |= test_gsm7_escape_boundary();
	failed |= test_part_count_limit();

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
	int total_parts;
	if (pdu_encode_multipart("", "+1234", long_ucs2, 0x11, 1,
				 &total_parts, pdu, sizeof(pdu)) < 0 ||
	    total_parts != 2) {
		fprintf(stderr, "multipart UCS-2 message was rejected\n");
		failed = 1;
	}

	return failed;
}
