#include <test.h>

#include <sysinfo.h>
#include <sysinfo.c>

static void test_uptime(void)
{
    /*
     * Assume we have been online at least one minute, and less than 5 years.
     * Should be good enough for everyone...
     */
    int uptime = GetUptimeMinutes(time(NULL));
    printf("Uptime: %.2f days\n", uptime / (60.0 * 24));
    assert_in_range(uptime, 1, 60*24*365*5);
}

static void FindNextIntegerTestWrapper(char *str, char expected[][KIBIBYTE(1)], int num_expected)
{
    Seq *seq = SeqNew(num_expected, NULL);
    char *integer;

    char *next = FindNextInteger(str, &integer);
    if (integer != NULL)
    {
        SeqAppend(seq, integer);
    }
    while (next != NULL && integer != NULL)
    {
        next = FindNextInteger(next, &integer);
        if (integer != NULL)
        {
            SeqAppend(seq, integer);
        }
    }

    assert_int_equal(num_expected, SeqLength(seq));
    for (int i = 0; i < num_expected; i++)
    {
        assert_string_equal((char *) SeqAt(seq, i), expected[i]);
    }

    SeqDestroy(seq);
}

static void test_find_next_integer(void)
{
    {
        char str[] = "Ubuntu 20.04.1 LTS";
        char expected[3][KIBIBYTE(1)] = { "20", "04", "1" };
        FindNextIntegerTestWrapper(str, expected, 3);
    }
    {
        char str[] = "canonified_5";
        char expected[1][KIBIBYTE(1)] = { "5" };
        FindNextIntegerTestWrapper(str, expected, 1);
    }
    {
        char str[] = "";
        char expected[0][KIBIBYTE(1)] = { };
        FindNextIntegerTestWrapper(str, expected, 0);
    }
    {
        char str[] = " ";
        char expected[0][KIBIBYTE(1)] = { };
        FindNextIntegerTestWrapper(str, expected, 0);
    }
    {
        char str[] = " no numbers in sight ";
        char expected[0][KIBIBYTE(1)] = { };
        FindNextIntegerTestWrapper(str, expected, 0);
    }
    {
        char str[] = "0";
        char expected[1][KIBIBYTE(1)] = { "0" };
        FindNextIntegerTestWrapper(str, expected, 1);
    }
    {
        char str[] = "1k";
        char expected[1][KIBIBYTE(1)] = { "1" };
        FindNextIntegerTestWrapper(str, expected, 1);
    }
    {
        char str[] = "1234";
        char expected[1][KIBIBYTE(1)] = { "1234" };
        FindNextIntegerTestWrapper(str, expected, 1);
    }
    {
        char str[] = "1 2 3 4";
        char expected[4][KIBIBYTE(1)] = { "1", "2", "3", "4" };
        FindNextIntegerTestWrapper(str, expected, 4);
    }
    {
        char str[] = "Debian 9";
        char expected[1][KIBIBYTE(1)] = { "9" };
        FindNextIntegerTestWrapper(str, expected, 1);
    }
    {
        char str[] = "Cent OS 6.7";
        char expected[2][KIBIBYTE(1)] = { "6", "7" };
        FindNextIntegerTestWrapper(str, expected, 2);
    }
    {
        char str[] = "CFEngine 3.18.0-2deadbeef";
        char expected[4][KIBIBYTE(1)] = { "3", "18", "0", "2" };
        FindNextIntegerTestWrapper(str, expected, 4);
    }
}

#ifdef __linux__
/*
 * GetNetworkingInfo() reads /proc/<pid>/net/route, relocated here through
 * CFENGINE_TEST_OVERRIDE_PROCDIR, and must pick the active default route
 * with the lowest metric even when that route is listed last.
 */
static void test_default_route_lowest_metric(void)
{
    char procdir[] = "/tmp/sysinfo_test_proc.XXXXXX";
    assert_true(mkdtemp(procdir) != NULL);

    char route_file[PATH_MAX];
    xsnprintf(route_file, sizeof(route_file),
              "%s/proc/42/net/route", procdir);
    assert_true(MakeParentDirectory(route_file, false, NULL));

    FILE *fp = fopen(route_file, "w");
    assert_true(fp != NULL);
    fputs("Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask"
          "\t\tMTU\tWindow\tIRTT\n"
          "eth0\t00000000\t0102A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n"
          "eth1\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n",
          fp);
    fclose(fp);

    setenv("CFENGINE_TEST_OVERRIDE_PROCDIR", procdir, 1);
    setenv("CFENGINE_TEST_OVERRIDE_PROCPID", "42", 1);

    EvalContext *ctx = EvalContextNew();
    GetNetworkingInfo(ctx);

    const JsonElement *inet = EvalContextVariableGetSpecial(
        ctx, SPECIAL_SCOPE_SYS, "inet", NULL, false);
    assert_true(inet != NULL);
    const char *gateway = JsonObjectGetAsString(inet, "default_gateway");
    assert_true(gateway != NULL);
    assert_string_equal(gateway, "192.168.1.1");

    EvalContextDestroy(ctx);
    unsetenv("CFENGINE_TEST_OVERRIDE_PROCDIR");
    unsetenv("CFENGINE_TEST_OVERRIDE_PROCPID");
    DeleteDirectoryTree(procdir);
    rmdir(procdir);
}
#endif /* __linux__ */

int main()
{
    PRINT_TEST_BANNER();
    const UnitTest tests[] =
    {
        unit_test(test_uptime),
        unit_test(test_find_next_integer),
#ifdef __linux__
        unit_test(test_default_route_lowest_metric),
#endif
    };

    return run_tests(tests);
}
