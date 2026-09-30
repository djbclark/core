/*
  Copyright 2026 Northern.tech AS

  This file is part of CFEngine 3 - written and maintained by Northern.tech AS.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; version 3.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of CFEngine, the applicable Commercial Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.
*/

#include <test.h>

#include <eval_context.h>       /* SetChangesChroot(), ToChangesChroot() */
#include <changes_chroot.h>     /* CHROOT_PKGS_OPS_FILE */
#include <file_lib.h>           /* DeleteDirectoryTree() */
#include <files_lib.h>          /* MakeParentDirectory() */
#include <json.h>               /* JsonParseFile() */
#include <string_sequence.h>    /* WriteLenPrefixedString() */
#include <writer.h>             /* FileWriter() */

#include <simulate_mode.h>

static char CHROOT_DIR[] = "/tmp/simulate_mode_test_chroot.XXXXXX";
static char ORIG_DIR[] = "/tmp/simulate_mode_test_orig.XXXXXX";
static char OUTPUT_FILE[PATH_MAX];

/* #csv contains "op,name,version,architecture" records terminated by "\r\n",
 * just like the records written by RecordPkgOperationInChroot(). */
static void write_pkgs_ops(const char *csv)
{
    FILE *file = fopen(ToChangesChroot(CHROOT_PKGS_OPS_FILE), "w");
    assert_true(file != NULL);
    assert_true(fputs(csv, file) >= 0);
    fclose(file);
}

/* Both DiffPkgOperations() and ManifestPkgOperations() print their reports
 * with puts(), so capture stdout into #output while calling #fn. */
static bool call_with_captured_stdout(bool (*fn)(void), char *output, size_t output_size)
{
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    assert_true(saved_stdout != -1);

    char out_file[] = "/tmp/simulate_mode_test_out.XXXXXX";
    int out_fd = mkstemp(out_file);
    assert_true(out_fd != -1);
    assert_true(dup2(out_fd, STDOUT_FILENO) != -1);

    const bool ret = fn();

    fflush(stdout);
    assert_true(dup2(saved_stdout, STDOUT_FILENO) != -1);
    close(saved_stdout);

    assert_true(lseek(out_fd, 0, SEEK_SET) != -1);
    const ssize_t n_read = read(out_fd, output, output_size - 1);
    assert_true(n_read >= 0);
    output[n_read] = '\0';
    close(out_fd);
    unlink(out_file);

    return ret;
}

/* A recorded removal followed by a recorded installation of the same package
 * is a net installation, so the removal must not be reported. */
static void test_diff_install_cancels_removal(void)
{
    write_pkgs_ops("r,foo,,\r\n"
                   "i,foo,1.2.3,\r\n");

    char output[4096];
    assert_true(call_with_captured_stdout(&DiffPkgOperations, output, sizeof(output)));

    assert_true(strstr(output, "Package 'foo [1.2.3]' would be installed") != NULL);
    assert_true(strstr(output, "would be removed") == NULL);

    unlink(ToChangesChroot(CHROOT_PKGS_OPS_FILE));
}

static void test_manifest_install_cancels_removal(void)
{
    write_pkgs_ops("r,foo,,\r\n"
                   "i,foo,1.2.3,\r\n");

    char output[4096];
    assert_true(call_with_captured_stdout(&ManifestPkgOperations, output, sizeof(output)));

    assert_true(strstr(output, "Package 'foo [1.2.3]' would be present") != NULL);
    assert_true(strstr(output, "would be absent") == NULL);

    unlink(ToChangesChroot(CHROOT_PKGS_OPS_FILE));
}

static void reset_records(void)
{
    unlink(ToChangesChroot(CHROOT_CHANGES_LIST_FILE));
    unlink(ToChangesChroot(CHROOT_RENAMES_LIST_FILE));
    unlink(OUTPUT_FILE);
}

static void write_records(const char *records_file,
                          const char *const records[], size_t n)
{
    FILE *file = fopen(ToChangesChroot(records_file), "w");
    assert_true(file != NULL);
    Writer *writer = FileWriter(file);
    for (size_t i = 0; i < n; i++)
    {
        assert_true(WriteLenPrefixedString(writer, records[i]));
    }
    WriterClose(writer);
}

static void create_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    assert_true(file != NULL);
    assert_true(fputs(content, file) >= 0);
    fclose(file);
}

static void create_chroot_file(const char *path, const char *content,
                               mode_t mode)
{
    char chrooted[PATH_MAX];
    strlcpy(chrooted, ToChangesChroot(path), sizeof(chrooted));
    assert_true(MakeParentDirectory(chrooted, true, NULL));

    create_file(chrooted, content);
    assert_int_equal(chmod(chrooted, mode), 0);
}

static JsonElement *write_and_parse_changes(void)
{
    assert_true(WriteChangesJson(OUTPUT_FILE, false));

    JsonElement *json = NULL;
    assert_int_equal(JsonParseFile(OUTPUT_FILE, 1024 * 1024, &json),
                     JSON_PARSE_OK);
    assert_true(json != NULL);
    return json;
}

/* Returns the only element of the #key array in #json. */
static JsonElement *get_single(JsonElement *json, const char *key)
{
    JsonElement *array = JsonObjectGetAsArray(json, key);
    assert_int_equal(JsonLength(array), 1);
    return JsonArrayGetAsObject(array, 0);
}

/* Reads the raw bytes of the output file. The encoding tests below need
 * them because JsonParseFile() decodes a "\u00XX" escape into the raw byte
 * 0xXX, so it cannot tell a (wrongly) escaped byte from a raw one. */
static void read_output_file_raw(char *buf, size_t buf_size)
{
    FILE *file = fopen(OUTPUT_FILE, "r");
    assert_true(file != NULL);
    size_t n_read = fread(buf, 1, buf_size - 1, file);
    fclose(file);
    assert_true(n_read > 0);
    buf[n_read] = '\0';
}

static void test_empty_change_set(void)
{
    reset_records();

    JsonElement *json = write_and_parse_changes();
    assert_int_equal(
        JsonPrimitiveGetAsInteger(JsonObjectGet(json, "format_version")), 1);
    assert_int_equal(JsonLength(JsonObjectGetAsArray(json, "files")), 0);
    assert_int_equal(JsonLength(JsonObjectGetAsArray(json, "renames")), 0);
    assert_true(JsonObjectGet(json, "failsafe_fallback") != NULL);
    assert_false(JsonObjectGetAsBool(json, "failsafe_fallback"));
    JsonDestroy(json);

    assert_true(WriteChangesJson(OUTPUT_FILE, true));
    assert_int_equal(JsonParseFile(OUTPUT_FILE, 1024 * 1024, &json),
                     JSON_PARSE_OK);
    assert_true(JsonObjectGetAsBool(json, "failsafe_fallback"));
    JsonDestroy(json);
}

static void test_created_file(void)
{
    reset_records();

    /* Recorded three times, reported once. */
    const char *const path = "/simulate-test/created \"file\" \\";
    const char *const paths[] = {path, path, path};
    create_chroot_file(path, "Hello, CFEngine!\n", 0640);
    write_records(CHROOT_CHANGES_LIST_FILE, paths, 3);

    struct stat st;
    assert_int_equal(lstat(ToChangesChroot(path), &st), 0);

    JsonElement *json = write_and_parse_changes();
    JsonElement *file_info = get_single(json, "files");
    assert_string_equal(JsonObjectGetAsString(file_info, "path"), path);
    assert_string_equal(JsonObjectGetAsString(file_info, "change"),
                        "created");
    assert_string_equal(JsonObjectGetAsString(file_info, "type"),
                        "regular file");
    assert_string_equal(JsonObjectGetAsString(file_info, "permissions"),
                        "0640");
    assert_int_equal(
        JsonPrimitiveGetAsInteger(JsonObjectGet(file_info, "uid")),
        (long) st.st_uid);
    assert_int_equal(
        JsonPrimitiveGetAsInteger(JsonObjectGet(file_info, "gid")),
        (long) st.st_gid);
    assert_int_equal(
        JsonPrimitiveGetAsInteger(JsonObjectGet(file_info, "size")), 17);
    assert_string_equal(
        JsonObjectGetAsString(file_info, "sha256"),
        "9be7023e1f91bae9d1f734b49c579cc2091c71924ae7494c9a5a3a8006527615");
    JsonDestroy(json);
}

static void test_deleted_file(void)
{
    reset_records();

    /* Exists outside of the chroot, but not in it. */
    char path[PATH_MAX];
    xsnprintf(path, sizeof(path), "%s/deleted-file", ORIG_DIR);
    create_file(path, "");
    const char *const paths[] = {path};
    write_records(CHROOT_CHANGES_LIST_FILE, paths, 1);

    JsonElement *json = write_and_parse_changes();
    JsonElement *file_info = get_single(json, "files");
    assert_string_equal(JsonObjectGetAsString(file_info, "path"), path);
    assert_string_equal(JsonObjectGetAsString(file_info, "change"),
                        "deleted");
    assert_true(JsonObjectGet(file_info, "type") == NULL);
    assert_true(JsonObjectGet(file_info, "sha256") == NULL);
    JsonDestroy(json);
}

static void test_created_and_deleted_file_not_reported(void)
{
    reset_records();

    /* Exists neither in the chroot nor outside of it. */
    const char *const path = "/simulate-test/created-and-deleted";
    write_records(CHROOT_CHANGES_LIST_FILE, &path, 1);

    JsonElement *json = write_and_parse_changes();
    assert_int_equal(JsonLength(JsonObjectGetAsArray(json, "files")), 0);
    JsonDestroy(json);
}

static void test_modified_file(void)
{
    reset_records();

    char path[PATH_MAX];
    xsnprintf(path, sizeof(path), "%s/modified-file", ORIG_DIR);
    create_file(path, "contents before the run\n");
    create_chroot_file(path, "new contents after the run\n", 0644);
    const char *const paths[] = {path};
    write_records(CHROOT_CHANGES_LIST_FILE, paths, 1);

    /* The digest is of the contents after the run. */
    JsonElement *json = write_and_parse_changes();
    JsonElement *file_info = get_single(json, "files");
    assert_string_equal(JsonObjectGetAsString(file_info, "change"),
                        "modified");
    assert_string_equal(
        JsonObjectGetAsString(file_info, "sha256"),
        "6ba024f3c03f13f9a8c1bb444829640c68553009facb418bb4552dd7ddebe427");
    JsonDestroy(json);
}

#ifndef __MINGW32__
static void test_created_symlink(void)
{
    reset_records();

    const char *const target = "/simulate-test/link-target";
    const char *const path = "/simulate-test/created-link";
    create_chroot_file(target, "", 0644);

    /* Links in the chroot point to chrooted paths. */
    char chrooted_target[PATH_MAX];
    strlcpy(chrooted_target, ToChangesChroot(target),
            sizeof(chrooted_target));
    assert_int_equal(symlink(chrooted_target, ToChangesChroot(path)), 0);
    write_records(CHROOT_CHANGES_LIST_FILE, &path, 1);

    JsonElement *json = write_and_parse_changes();
    JsonElement *file_info = get_single(json, "files");
    assert_string_equal(JsonObjectGetAsString(file_info, "type"),
                        "symbolic link");
    assert_string_equal(JsonObjectGetAsString(file_info, "target"), target);
    assert_true(JsonObjectGet(file_info, "permissions") == NULL);
    JsonDestroy(json);
}
#endif  /* !__MINGW32__ */

static void test_renamed_file(void)
{
    reset_records();

    const char *const names[] = {"/simulate-test/old", "/simulate-test/new"};
    write_records(CHROOT_RENAMES_LIST_FILE, names, 2);

    JsonElement *json = write_and_parse_changes();
    JsonElement *rename = get_single(json, "renames");
    assert_string_equal(JsonObjectGetAsString(rename, "old_name"), names[0]);
    assert_string_equal(JsonObjectGetAsString(rename, "new_name"), names[1]);
    JsonDestroy(json);

    /* An original name without the new name is an error. */
    write_records(CHROOT_RENAMES_LIST_FILE, names, 1);
    assert_false(WriteChangesJson(OUTPUT_FILE, false));
}

static void test_special_characters_in_path(void)
{
    reset_records();

    const char *const path = "/simulate-test/w\xC3\xA9" "ird \"file\" \\ name";
    create_chroot_file(path, "", 0600);
    write_records(CHROOT_CHANGES_LIST_FILE, &path, 1);

    JsonElement *json = write_and_parse_changes();
    JsonElement *files = JsonObjectGetAsArray(json, "files");
    assert_int_equal(JsonLength(files), 1);

    /* The path must survive JSON escaping and parsing untouched. */
    JsonElement *file_info = JsonArrayGetAsObject(files, 0);
    assert_string_equal(JsonObjectGetAsString(file_info, "path"), path);
    assert_string_equal(JsonObjectGetAsString(file_info, "change"), "created");

    JsonDestroy(json);

    /* The UTF-8 character must appear in the document as its raw bytes --
     * as per-byte "\u00XX" escapes, a conformant JSON parser would decode
     * it as two wrong characters (the parser above reverses such escapes,
     * so it cannot detect them). */
    char raw[4096];
    read_output_file_raw(raw, sizeof(raw));
    assert_true(strstr(raw, "w\xC3\xA9" "ird") != NULL);
    assert_true(strstr(raw, "\\u00c3") == NULL);
}

static void test_invalid_utf8_in_path(void)
{
    reset_records();

    /* File names are not guaranteed to be valid UTF-8. A byte that is not
     * part of a valid UTF-8 sequence has to stay escaped as "\u00XX" --
     * raw, it would make the whole document invalid UTF-8. No file is
     * created here (the file system may refuse such a name), so the name
     * is recorded as a rename, which needs no file to get it into the
     * document. */
    const char *const names[] = {"/simulate-test/latin1-\xE9-name",
                                 "/simulate-test/new-name"};
    write_records(CHROOT_RENAMES_LIST_FILE, names, 2);

    JsonElement *json = write_and_parse_changes();
    assert_string_equal(
        JsonObjectGetAsString(get_single(json, "renames"), "old_name"),
        names[0]);
    JsonDestroy(json);

    char raw[4096];
    read_output_file_raw(raw, sizeof(raw));
    assert_true(strstr(raw, "\\u00e9") != NULL);
}

static void test_write_failure(void)
{
    reset_records();

    char bad_output[PATH_MAX];
    xsnprintf(bad_output, sizeof(bad_output), "%s/no-such-dir/out.json",
              ORIG_DIR);
    assert_false(WriteChangesJson(bad_output, false));
}

#ifndef __MINGW32__
static void test_output_symlink_not_followed(void)
{
    reset_records();

    char link_target[PATH_MAX];
    xsnprintf(link_target, sizeof(link_target), "%s/link-target", ORIG_DIR);
    create_file(link_target, "do not overwrite\n");
    assert_int_equal(symlink(link_target, OUTPUT_FILE), 0);

    JsonDestroy(write_and_parse_changes());

    struct stat st;
    assert_int_equal(lstat(OUTPUT_FILE, &st), 0);
    assert_true(S_ISREG(st.st_mode));

    char buf[64] = {0};
    FILE *file = fopen(link_target, "r");
    assert_true(file != NULL);
    assert_true(fread(buf, 1, sizeof(buf) - 1, file) > 0);
    fclose(file);
    assert_string_equal(buf, "do not overwrite\n");
}
#endif  /* !__MINGW32__ */

int main()
{
    PRINT_TEST_BANNER();

    assert_true(mkdtemp(CHROOT_DIR) != NULL);
    SetChangesChroot(CHROOT_DIR);
    assert_true(mkdtemp(ORIG_DIR) != NULL);
    xsnprintf(OUTPUT_FILE, sizeof(OUTPUT_FILE), "%s/out.json", ORIG_DIR);

    const UnitTest tests[] =
    {
        unit_test(test_diff_install_cancels_removal),
        unit_test(test_manifest_install_cancels_removal),
        unit_test(test_empty_change_set),
        unit_test(test_created_file),
        unit_test(test_deleted_file),
        unit_test(test_created_and_deleted_file_not_reported),
        unit_test(test_modified_file),
        unit_test(test_special_characters_in_path),
        unit_test(test_invalid_utf8_in_path),
#ifndef __MINGW32__
        unit_test(test_created_symlink),
#endif
        unit_test(test_renamed_file),
        unit_test(test_write_failure),
#ifndef __MINGW32__
        unit_test(test_output_symlink_not_followed),
#endif
    };

    int ret = run_tests(tests);

    DeleteDirectoryTree(CHROOT_DIR);
    rmdir(CHROOT_DIR);
    DeleteDirectoryTree(ORIG_DIR);
    rmdir(ORIG_DIR);

    return ret;
}
