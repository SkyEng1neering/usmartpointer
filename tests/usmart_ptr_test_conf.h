/*
 * dalloc configuration for usmart_ptr tests
 * Keep minimal - use defaults from dalloc_conf.h
 */

#ifndef USMART_PTR_TEST_CONF_H
#define USMART_PTR_TEST_CONF_H

/* Enable single heap mode */
#define USE_SINGLE_HEAP_MEMORY

/* Disable debug output during tests */
#define dalloc_debug(...) ((void)0)

#endif /* USMART_PTR_TEST_CONF_H */
