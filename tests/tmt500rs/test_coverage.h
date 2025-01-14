#ifndef __TEST_COVERAGE_H
#define __TEST_COVERAGE_H

#include <linux/kernel.h>

/* Test coverage categories */
enum test_category {
    TC_DEVICE_INIT,
    TC_FORCE_FEEDBACK,
    TC_ERROR_HANDLING,
    TC_RESOURCE_MGMT,
    TC_EFFECT_COMBO,
    TC_EDGE_CASES,
    TC_MAX_CATEGORIES
};

/* Test coverage statistics */
struct test_coverage {
    const char *name;
    int total_tests;
    int executed_tests;
    int passed_tests;
    int failed_tests;
    bool enabled;
};

/* Test coverage report */
struct test_coverage_report {
    struct test_coverage categories[TC_MAX_CATEGORIES];
    int total_tests;
    int total_executed;
    int total_passed;
    int total_failed;
};

/* Initialize test coverage report */
static inline void init_test_coverage(struct test_coverage_report *report)
{
    memset(report, 0, sizeof(*report));

    /* Initialize categories */
    report->categories[TC_DEVICE_INIT] = (struct test_coverage){
        .name = "Device Initialization",
        .enabled = true,
    };
    report->categories[TC_FORCE_FEEDBACK] = (struct test_coverage){
        .name = "Force Feedback Effects",
        .enabled = true,
    };
    report->categories[TC_ERROR_HANDLING] = (struct test_coverage){
        .name = "Error Handling",
        .enabled = true,
    };
    report->categories[TC_RESOURCE_MGMT] = (struct test_coverage){
        .name = "Resource Management",
        .enabled = true,
    };
    report->categories[TC_EFFECT_COMBO] = (struct test_coverage){
        .name = "Effect Combinations",
        .enabled = true,
    };
    report->categories[TC_EDGE_CASES] = (struct test_coverage){
        .name = "Edge Cases",
        .enabled = true,
    };
}

/* Record test result */
static inline void record_test_result(struct test_coverage_report *report,
                                    enum test_category category,
                                    bool passed)
{
    if (category >= TC_MAX_CATEGORIES)
        return;

    struct test_coverage *cat = &report->categories[category];
    if (!cat->enabled)
        return;

    cat->total_tests++;
    cat->executed_tests++;
    if (passed)
        cat->passed_tests++;
    else
        cat->failed_tests++;

    /* Update totals */
    report->total_tests++;
    report->total_executed++;
    if (passed)
        report->total_passed++;
    else
        report->total_failed++;
}

/* Print test coverage report */
static inline void print_test_coverage_report(struct test_coverage_report *report)
{
    int i;
    pr_info("\n=== Test Coverage Report ===\n");

    for (i = 0; i < TC_MAX_CATEGORIES; i++) {
        struct test_coverage *cat = &report->categories[i];
        if (!cat->enabled)
            continue;

        pr_info("%s:\n", cat->name);
        pr_info("  Tests: %d, Executed: %d, Passed: %d, Failed: %d\n",
                cat->total_tests, cat->executed_tests,
                cat->passed_tests, cat->failed_tests);
        pr_info("  Coverage: %d%%\n",
                cat->total_tests > 0 ?
                (cat->executed_tests * 100) / cat->total_tests : 0);
    }

    pr_info("\nOverall Coverage:\n");
    pr_info("Total Tests: %d\n", report->total_tests);
    pr_info("Executed: %d\n", report->total_executed);
    pr_info("Passed: %d\n", report->total_passed);
    pr_info("Failed: %d\n", report->total_failed);
    pr_info("Coverage: %d%%\n",
            report->total_tests > 0 ?
            (report->total_executed * 100) / report->total_tests : 0);
}

#endif /* __TEST_COVERAGE_H */ 