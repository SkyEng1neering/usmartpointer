/*
 * dalloc configuration for usmart_ptr tests
 * Keep minimal - use defaults from dalloc_conf.h
 */

#ifndef USMART_PTR_TEST_CONF_H
#define USMART_PTR_TEST_CONF_H

/* Enable single heap mode */
#define USE_SINGLE_HEAP_MEMORY

/* 8-byte alignment: usmart_ptr<T> contains pointer members that require
 * 8-byte alignment on 64-bit systems. Also matches production config. */
#define ALLOCATION_ALIGNMENT_BYTES  8U

/* Disable debug output during tests */
#define dalloc_debug(...) ((void)0)

#endif /* USMART_PTR_TEST_CONF_H */
