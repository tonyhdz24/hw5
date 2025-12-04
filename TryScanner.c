// TryScanner.c - Comprehensive test suite for Scanner device driver
// gcc -o TryScanner TryScanner.c -g -Wall

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define DEVICE "/dev/Hello"
#define ERR(s) err(s, __FILE__, __LINE__)

static int tests_passed = 0;
static int tests_failed = 0;

static void err(char *s, char *file, int line)
{
  fprintf(stderr, "%s:%d: %s\n", file, line, s);
  exit(1);
}

// Read a complete token into buffer, returns total length or -1
static int read_token(int fd, char *buf, int max)
{
  int total = 0;
  int len;

  while ((len = read(fd, buf + total, max - total)) > 0)
  {
    total += len;
  }

  if (len == 0)
  {
    // End of token
    buf[total] = '\0';
    return total;
  }

  // len == -1 means end of data
  if (total > 0)
  {
    buf[total] = '\0';
    return total;
  }
  return -1;
}

static void test_result(const char *name, int passed)
{
  if (passed)
  {
    printf("  [PASS] %s\n", name);
    tests_passed++;
  }
  else
  {
    printf("  [FAIL] %s\n", name);
    tests_failed++;
  }
}

// Test 1: Basic tokenization with default separators
void test_basic_tokens(void)
{
  printf("\nTest 1: Basic tokenization with default separators\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  char *data = "hello world foo";
  write(fd, data, strlen(data));

  char buf[100];
  int len;

  len = read_token(fd, buf, 100);
  test_result("First token is 'hello'", len == 5 && strcmp(buf, "hello") == 0);

  len = read_token(fd, buf, 100);
  test_result("Second token is 'world'", len == 5 && strcmp(buf, "world") == 0);

  len = read_token(fd, buf, 100);
  test_result("Third token is 'foo'", len == 3 && strcmp(buf, "foo") == 0);

  len = read_token(fd, buf, 100);
  test_result("No more tokens (returns -1)", len == -1);

  close(fd);
}

// Test 2: Custom separators
void test_custom_separators(void)
{
  printf("\nTest 2: Custom separators (colon)\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  // Set custom separator to colon
  if (ioctl(fd, 0, 0))
    ERR("ioctl() failed");
  if (write(fd, ":", 1) != 1)
    ERR("write() of separators failed");

  char *data = "root:x:0:0:root:/root:/bin/bash";
  write(fd, data, strlen(data));

  char buf[100];
  char *expected[] = {"root", "x", "0", "0", "root", "/root", "/bin/bash"};

  for (int i = 0; i < 7; i++)
  {
    int len = read_token(fd, buf, 100);
    char msg[100];
    snprintf(msg, 100, "Token %d is '%s'", i, expected[i]);
    test_result(msg, len == (int)strlen(expected[i]) && strcmp(buf, expected[i]) == 0);
  }

  int len = read_token(fd, buf, 100);
  test_result("No more tokens", len == -1);

  close(fd);
}

// Test 3: Small buffer (token larger than buffer)
void test_small_buffer(void)
{
  printf("\nTest 3: Small buffer reads (partial tokens)\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  char *data = "hello world";
  write(fd, data, strlen(data));

  char buf[3];
  int len;

  // Read "hello" in chunks
  len = read(fd, buf, 2);
  buf[len] = '\0';
  test_result("First partial read is 'he'", len == 2 && strcmp(buf, "he") == 0);

  len = read(fd, buf, 2);
  buf[len] = '\0';
  test_result("Second partial read is 'll'", len == 2 && strcmp(buf, "ll") == 0);

  len = read(fd, buf, 2);
  buf[len] = '\0';
  test_result("Third partial read is 'o'", len == 1 && strcmp(buf, "o") == 0);

  len = read(fd, buf, 2);
  test_result("End of token (returns 0)", len == 0);

  // Now read "world"
  char buf2[10];
  len = read_token(fd, buf2, 10);
  test_result("Next token is 'world'", len == 5 && strcmp(buf2, "world") == 0);

  close(fd);
}

// Test 4: Multiple consecutive separators
void test_multiple_separators(void)
{
  printf("\nTest 4: Multiple consecutive separators\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  char *data = "a   b\t\t\nc";
  write(fd, data, strlen(data));

  char buf[100];

  int len = read_token(fd, buf, 100);
  test_result("First token is 'a'", len == 1 && strcmp(buf, "a") == 0);

  len = read_token(fd, buf, 100);
  test_result("Second token is 'b'", len == 1 && strcmp(buf, "b") == 0);

  len = read_token(fd, buf, 100);
  test_result("Third token is 'c'", len == 1 && strcmp(buf, "c") == 0);

  len = read_token(fd, buf, 100);
  test_result("No more tokens", len == -1);

  close(fd);
}

// Test 5: Leading and trailing separators
void test_leading_trailing_seps(void)
{
  printf("\nTest 5: Leading and trailing separators\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  char *data = "   hello   ";
  write(fd, data, strlen(data));

  char buf[100];

  int len = read_token(fd, buf, 100);
  test_result("Token is 'hello'", len == 5 && strcmp(buf, "hello") == 0);

  len = read_token(fd, buf, 100);
  test_result("No more tokens", len == -1);

  close(fd);
}

// Test 6: NUL character handling (requirement 7)
void test_nul_handling(void)
{
  printf("\nTest 6: NUL character handling\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  // Set separator to NUL
  if (ioctl(fd, 0, 0))
    ERR("ioctl() failed");
  char nul = '\0';
  if (write(fd, &nul, 1) != 1)
    ERR("write() of NUL separator failed");

  // Data with NUL as separator: "hello\0world"
  char data[] = {'h', 'e', 'l', 'l', 'o', '\0', 'w', 'o', 'r', 'l', 'd'};
  write(fd, data, 11);

  char buf[100];

  int len = read_token(fd, buf, 100);
  test_result("First token is 'hello'", len == 5 && memcmp(buf, "hello", 5) == 0);

  len = read_token(fd, buf, 100);
  test_result("Second token is 'world'", len == 5 && memcmp(buf, "world", 5) == 0);

  close(fd);
}

// Test 7: NUL in data (not as separator)
void test_nul_in_data(void)
{
  printf("\nTest 7: NUL in data (not as separator)\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  // Default separators (space, tab, newline)
  // Data: "ab\0c d" where NUL is part of token
  char data[] = {'a', 'b', '\0', 'c', ' ', 'd'};
  write(fd, data, 6);

  char buf[100];

  int len = read_token(fd, buf, 100);
  test_result("First token contains NUL (len=4)", len == 4 && memcmp(buf, "ab\0c", 4) == 0);

  len = read_token(fd, buf, 100);
  test_result("Second token is 'd'", len == 1 && buf[0] == 'd');

  close(fd);
}

// Test 8: Empty data
void test_empty_data(void)
{
  printf("\nTest 8: Empty and separator-only data\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  // Write separators only
  char *data = "   \t\n  ";
  write(fd, data, strlen(data));

  char buf[100];
  int len = read_token(fd, buf, 100);
  test_result("Separator-only data returns -1", len == -1);

  close(fd);
}

// Test 9: Multiple writes replace data
void test_write_replaces_data(void)
{
  printf("\nTest 9: Each write() replaces data\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  char *data1 = "first";
  write(fd, data1, strlen(data1));

  char *data2 = "second";
  write(fd, data2, strlen(data2));

  char buf[100];
  int len = read_token(fd, buf, 100);
  test_result("Only 'second' is returned", len == 6 && strcmp(buf, "second") == 0);

  len = read_token(fd, buf, 100);
  test_result("No more tokens", len == -1);

  close(fd);
}

// Test 10: Multiple instances
void test_multiple_instances(void)
{
  printf("\nTest 10: Multiple concurrent instances\n");

  int fd1 = open(DEVICE, O_RDWR);
  int fd2 = open(DEVICE, O_RDWR);
  if (fd1 < 0 || fd2 < 0)
    ERR("open() failed");

  // Set different separators for each
  ioctl(fd1, 0, 0);
  write(fd1, ":", 1);

  ioctl(fd2, 0, 0);
  write(fd2, "-", 1);

  // Write different data
  char *data1 = "a:b:c";
  char *data2 = "x-y-z";
  write(fd1, data1, strlen(data1));
  write(fd2, data2, strlen(data2));

  char buf[100];

  // Read from fd1
  int len = read_token(fd1, buf, 100);
  test_result("fd1 first token is 'a'", len == 1 && buf[0] == 'a');

  // Read from fd2
  len = read_token(fd2, buf, 100);
  test_result("fd2 first token is 'x'", len == 1 && buf[0] == 'x');

  // Continue fd1
  len = read_token(fd1, buf, 100);
  test_result("fd1 second token is 'b'", len == 1 && buf[0] == 'b');

  close(fd1);
  close(fd2);
}

// Test 11: Binary data with multiple separator bytes
void test_binary_separators(void)
{
  printf("\nTest 11: Multiple separator bytes\n");

  int fd = open(DEVICE, O_RDWR);
  if (fd < 0)
    ERR("open() failed");

  // Set separators to ",-"
  ioctl(fd, 0, 0);
  write(fd, ",-", 2);

  char *data = "a,b-c,d";
  write(fd, data, strlen(data));

  char buf[100];
  char *expected[] = {"a", "b", "c", "d"};

  for (int i = 0; i < 4; i++)
  {
    int len = read_token(fd, buf, 100);
    char msg[50];
    snprintf(msg, 50, "Token %d is '%s'", i, expected[i]);
    test_result(msg, len == 1 && buf[0] == expected[i][0]);
  }

  close(fd);
}

int main()
{
  printf("Scanner Device Driver Test Suite\n");
  printf("=================================\n");

  test_basic_tokens();
  test_custom_separators();
  test_small_buffer();
  test_multiple_separators();
  test_leading_trailing_seps();
  test_nul_handling();
  test_nul_in_data();
  test_empty_data();
  test_write_replaces_data();
  test_multiple_instances();
  test_binary_separators();

  printf("\n=================================\n");
  printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

  return tests_failed > 0 ? 1 : 0;
}