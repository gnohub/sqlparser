#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

static int check_valid(const char *text, size_t limit, size_t expected_length)
{
	sqlparser_error_t error;
	size_t length = SIZE_MAX;

	memset(&error, 0, sizeof(error));
	error.code = SQLPARSER_STATUS_PARSE_ERROR;
	strcpy(error.message, "unchanged");
	CHECK(sqlparser_validate_text_limit(text, limit, NULL, &length, &error) == SQLPARSER_STATUS_OK);
	CHECK(length == expected_length);
	CHECK(error.code == SQLPARSER_STATUS_PARSE_ERROR && strcmp(error.message, "unchanged") == 0);
	CHECK(sqlparser_validate_text_limit(text, limit, NULL, NULL, NULL) == SQLPARSER_STATUS_OK);
	return 0;
}

static int check_invalid(const char *text, size_t limit, const char *field,
	sqlparser_status_t expected_status, const char *expected_message)
{
	sqlparser_error_t error;
	size_t length = SIZE_MAX;

	memset(&error, 0, sizeof(error));
	CHECK(sqlparser_validate_text_limit(text, limit, field, &length, &error) == expected_status);
	CHECK(length == 0U && error.code == expected_status);
	CHECK(strcmp(error.message, expected_message) == 0);
	length = SIZE_MAX;
	CHECK(sqlparser_validate_text_limit(text, limit, field, &length, NULL) == expected_status);
	CHECK(length == 0U);
	return 0;
}

int main(void)
{
	char embedded_nul[] = { 'a', '\0', 'b', 'c' };
	char *unterminated;
	int result;

	CHECK(check_invalid(NULL, 8U, NULL, SQLPARSER_STATUS_INVALID_ARGUMENT,
		"text must not be NULL") == 0);
	CHECK(check_valid("", 0U, 0U) == 0);
	CHECK(check_valid("", 1U, 0U) == 0);
	CHECK(check_valid("1234567", 8U, 7U) == 0);
	CHECK(check_valid("12345678", 8U, 8U) == 0);
	CHECK(check_invalid("123456789", 8U, "SQL input", SQLPARSER_STATUS_RESOURCE_LIMIT,
		"SQL input exceeds configured byte limit (8 bytes)") == 0);
	CHECK(check_invalid("abc", 2U, NULL, SQLPARSER_STATUS_RESOURCE_LIMIT,
		"text exceeds configured byte limit (2 bytes)") == 0);
	CHECK(check_valid("123456789", 0U, 9U) == 0);
	CHECK(check_valid("123456789", SIZE_MAX, 9U) == 0);
	CHECK(check_valid("123456789", SIZE_MAX - 1U, 9U) == 0);
	CHECK(check_valid(embedded_nul, 1U, 1U) == 0);
	CHECK(check_valid("\xC3\xA9", 2U, 2U) == 0);
	CHECK(check_invalid("\xC3\xA9", 1U, NULL, SQLPARSER_STATUS_RESOURCE_LIMIT,
		"text exceeds configured byte limit (1 bytes)") == 0);
	/* With ASan this allocation has an immediate redzone: rejection must not
	 * read beyond the limit plus the one byte used to reject it. */
	unterminated = (char *)malloc(9U);
	CHECK(unterminated != NULL);
	memset(unterminated, 'x', 9U);
	result = check_invalid(unterminated, 8U, NULL, SQLPARSER_STATUS_RESOURCE_LIMIT,
		"text exceeds configured byte limit (8 bytes)");
	free(unterminated);
	CHECK(result == 0);
	puts("Text byte-limit boundaries and bounded scans passed");
	return 0;
}
