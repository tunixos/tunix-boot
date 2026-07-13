#include "harness.h"
#include "config/config.h"

static struct config config;
static struct config_error error;

static bool parse(const char *text) {
    return config_parse(text, cstr_length(text), &config, &error);
}

static bool value_is(struct str text, const char *expected) {
    return str_equal_cstr(text, expected);
}

static void parses_a_configuration(void) {
    CHECK(parse("# what to boot\n"
                "timeout = 10\n"
                "default = tunix\n"
                "\n"
                ":tunix\n"
                "kernel = /kernel.elf\n"
                "cmdline = root=/dev/sda1 quiet\n"
                "module = /initramfs.img\n"));

    CHECK(config.timeout_seconds == 10);
    CHECK(config.entry_count == 1);
    CHECK(value_is(config.entries[0].name, "tunix"));
    CHECK(value_is(config.entries[0].kernel, "/kernel.elf"));
    /* The value keeps its own separators: only the first one splits the line. */
    CHECK(value_is(config.entries[0].cmdline, "root=/dev/sda1 quiet"));
    CHECK(config.entries[0].module_count == 1);
    CHECK(value_is(config.entries[0].modules[0], "/initramfs.img"));
}

static void a_missing_timeout_has_a_default(void) {
    CHECK(parse(":a\nkernel = /a.elf\n"));
    CHECK(config.timeout_seconds == CONFIG_DEFAULT_TIMEOUT_SECONDS);
}

static void tolerates_whitespace_and_blank_lines(void) {
    CHECK(parse("\n\n   timeout=3   \n"
                "\t\n"
                "   :spaced   \n"
                "   kernel   =   /k.elf   \n"));

    CHECK(config.timeout_seconds == 3);
    CHECK(value_is(config.entries[0].name, "spaced"));
    CHECK(value_is(config.entries[0].kernel, "/k.elf"));
}

static void handles_carriage_returns(void) {
    /* A file written on another system must not have its last character become
       part of every value. */
    CHECK(parse("timeout = 7\r\n:a\r\nkernel = /a.elf\r\n"));
    CHECK(config.timeout_seconds == 7);
    CHECK(value_is(config.entries[0].kernel, "/a.elf"));
}

static void a_file_without_a_final_newline_still_parses(void) {
    CHECK(parse(":a\nkernel = /a.elf"));
    CHECK(config.entry_count == 1);
    CHECK(value_is(config.entries[0].kernel, "/a.elf"));
}

static void an_empty_file_has_no_entries(void) {
    CHECK(parse(""));
    CHECK(config.entry_count == 0);
    CHECK(config_default_entry(&config) == NULL);
}

static void keeps_several_entries_apart(void) {
    CHECK(parse(":first\nkernel = /1.elf\n"
                ":second\nkernel = /2.elf\ncmdline = second\n"
                ":third\nkernel = /3.elf\n"));

    CHECK(config.entry_count == 3);
    CHECK(value_is(config.entries[1].kernel, "/2.elf"));
    /* A setting belongs to the entry it follows, and to no other. */
    CHECK(config.entries[0].cmdline.length == 0);
    CHECK(config.entries[2].cmdline.length == 0);
    CHECK(value_is(config.entries[1].cmdline, "second"));
}

static void finds_the_default_entry(void) {
    CHECK(parse("default = second\n"
                ":first\nkernel = /1.elf\n"
                ":second\nkernel = /2.elf\n"));

    const struct config_entry *entry = config_default_entry(&config);
    CHECK(entry != NULL);
    CHECK(value_is(entry->kernel, "/2.elf"));
    CHECK(config_find(&config, str_from_cstr("first")) == &config.entries[0]);
    CHECK(config_find(&config, str_from_cstr("nothere")) == NULL);
}

static void without_a_default_the_first_entry_wins(void) {
    CHECK(parse(":first\nkernel = /1.elf\n:second\nkernel = /2.elf\n"));
    CHECK(config_default_entry(&config) == &config.entries[0]);
}

static void a_default_naming_nothing_is_not_quietly_the_first(void) {
    CHECK(parse("default = missing\n:first\nkernel = /1.elf\n"));
    /* Booting the first entry because the named one is absent boots something
       other than what the file asked for. */
    CHECK(config_default_entry(&config) == NULL);
}

static void several_modules_in_one_entry(void) {
    CHECK(parse(":a\nkernel = /k.elf\n"
                "module = /one\nmodule = /two\nmodule = /three\n"));

    CHECK(config.entries[0].module_count == 3);
    CHECK(value_is(config.entries[0].modules[2], "/three"));
}

static void refuses_a_line_it_does_not_understand(void) {
    CHECK(!parse(":a\nkernel = /k.elf\nthis is not a setting\n"));
    CHECK(error.line == 3);
    CHECK(error.reason != NULL);
}

static void refuses_an_unknown_setting(void) {
    CHECK(!parse(":a\nkernel = /k.elf\nkernal = /typo.elf\n"));
    /* A typo silently ignored is a machine that boots the wrong kernel. */
    CHECK(error.line == 3);

    CHECK(!parse("timeuot = 5\n:a\nkernel = /k.elf\n"));
    CHECK(error.line == 1);
}

static void refuses_a_setting_with_no_value(void) {
    CHECK(!parse(":a\nkernel =\n"));
    CHECK(error.line == 2);

    CHECK(!parse(":a\n= /k.elf\n"));
    CHECK(error.line == 2);
}

static void refuses_an_entry_with_no_kernel(void) {
    CHECK(!parse(":a\ncmdline = quiet\n"));
    CHECK(error.reason != NULL);

    CHECK(!parse(":a\nkernel = /a.elf\n:b\ncmdline = quiet\n"));
}

static void refuses_an_entry_with_no_name(void) {
    CHECK(!parse(":\nkernel = /k.elf\n"));
    CHECK(error.line == 1);
    CHECK(!parse(":   \nkernel = /k.elf\n"));
}

static void refuses_two_entries_of_one_name(void) {
    CHECK(!parse(":a\nkernel = /1.elf\n:a\nkernel = /2.elf\n"));
    /* Which one `default = a` meant would be whichever came first. */
    CHECK(error.line == 3);
}

static void refuses_a_timeout_that_is_not_a_number(void) {
    CHECK(!parse("timeout = soon\n:a\nkernel = /k.elf\n"));
    CHECK(error.line == 1);

    CHECK(!parse("timeout = 999999999\n:a\nkernel = /k.elf\n"));
    CHECK(error.line == 1);
}

static void refuses_more_entries_than_it_holds(void) {
    static char text[4096];
    size_t at = 0;
    for (unsigned index = 0; index <= CONFIG_MAX_ENTRIES; index++) {
        /* Each entry named after its number, so none collide. */
        text[at++] = ':';
        text[at++] = 'e';
        text[at++] = (char)('a' + index);
        text[at++] = '\n';
        const char *line = "kernel = /k.elf\n";
        for (size_t index2 = 0; line[index2]; index2++) text[at++] = line[index2];
    }
    text[at] = '\0';

    CHECK(!config_parse(text, at, &config, &error));
    CHECK(error.reason != NULL);
}

static void refuses_more_modules_than_it_holds(void) {
    static char text[1024];
    size_t at = 0;
    const char *head = ":a\nkernel = /k.elf\n";
    for (size_t index = 0; head[index]; index++) text[at++] = head[index];
    for (unsigned index = 0; index <= CONFIG_MAX_MODULES; index++) {
        const char *line = "module = /m\n";
        for (size_t index2 = 0; line[index2]; index2++) text[at++] = line[index2];
    }

    CHECK(!config_parse(text, at, &config, &error));
}

static void refuses_a_file_too_large_to_be_one(void) {
    static char text[CONFIG_MAX_BYTES + 16U];
    for (size_t index = 0; index < sizeof text; index++) text[index] = '\n';
    CHECK(!config_parse(text, sizeof text, &config, &error));
}

static void a_comment_is_not_a_setting(void) {
    CHECK(parse("# timeout = 99\n"
                ":a\n"
                "kernel = /k.elf\n"
                "# kernel = /wrong.elf\n"));
    CHECK(config.timeout_seconds == CONFIG_DEFAULT_TIMEOUT_SECONDS);
    CHECK(value_is(config.entries[0].kernel, "/k.elf"));
}

TEST_MAIN(
    parses_a_configuration();
    a_missing_timeout_has_a_default();
    tolerates_whitespace_and_blank_lines();
    handles_carriage_returns();
    a_file_without_a_final_newline_still_parses();
    an_empty_file_has_no_entries();
    keeps_several_entries_apart();
    finds_the_default_entry();
    without_a_default_the_first_entry_wins();
    a_default_naming_nothing_is_not_quietly_the_first();
    several_modules_in_one_entry();
    refuses_a_line_it_does_not_understand();
    refuses_an_unknown_setting();
    refuses_a_setting_with_no_value();
    refuses_an_entry_with_no_kernel();
    refuses_an_entry_with_no_name();
    refuses_two_entries_of_one_name();
    refuses_a_timeout_that_is_not_a_number();
    refuses_more_entries_than_it_holds();
    refuses_more_modules_than_it_holds();
    refuses_a_file_too_large_to_be_one();
    a_comment_is_not_a_setting();
)
