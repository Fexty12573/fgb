#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tester.h>

extern struct tester_operations mock_cpu_ops;

static struct tester_flags flags = {
    .keep_going_on_mismatch = 0,
    .enable_cb_instruction_testing = 1,
    .print_tested_instruction = 0,
    .print_verbose_inputs = 0,
};

static void print_usage(char* progname) {
    printf("Usage: %s [option]...\n\n", progname);
    printf("Game Boy Instruction Tester.\n\n");
    printf("Options:\n");
    printf(" -k     Skip to the next instruction on a mismatch (instead of aborting all tests).\n");
    printf(" -c     Disable testing of CB prefixed instructions.\n");
    printf(" -p     Print instruction undergoing tests.\n");
    printf(" -v     Print every inputstate that is tested.\n");
    printf(" -h     Show this help.\n");
}

static int parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }

        if (strchr(argv[i] + 1, 'k')) {
            flags.keep_going_on_mismatch = 1;
        }
        if (strchr(argv[i] + 1, 'c')) {
            flags.enable_cb_instruction_testing = 0;
        }
        if (strchr(argv[i] + 1, 'p')) {
            flags.print_tested_instruction = 1;
        }
        if (strchr(argv[i] + 1, 'v')) {
            flags.print_verbose_inputs = 1;
        }
        if (argv[i][1] == 'h') {
            print_usage(argv[0]);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    if (parse_args(argc, argv))
        return 1;

    return tester_run(&flags, &mock_cpu_ops);
}
