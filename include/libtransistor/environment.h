/**
 * @file libtransistor/environment.h
 * @brief Functions to query the current environment
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include<libtransistor/types.h>

typedef enum {
	TARGET_VERSION_INVALID,
	TARGET_VERSION_1_0_0,
	TARGET_VERSION_2_0_0,
	TARGET_VERSION_3_0_0,
	TARGET_VERSION_4_0_0,
	TARGET_VERSION_5_0_0,
	TARGET_VERSION_11_0_0,
	TARGET_VERSION_MAX = TARGET_VERSION_11_0_0,
} target_version_t;

/**
 * @brief Returns the current target version, for feature-detection purposes.
 */
result_t env_get_target_version(target_version_t *version);


/**
 * @brief Infers the target version using set:sys.
 */
result_t env_infer_target_version_by_set_sys();


/**
 * @brief Sets the target version.
 */
result_t env_set_target_version(target_version_t version);


/**
 * @brief Returns the kernel's compatible target version, for SVC compatibility only.
 */
target_version_t env_get_svc_version();

/**
 * @brief Gets a pointer to the top of the stack
 */
void *env_get_stack_top();


/**
 * @brief Returns the ASLR base
 */
void *env_get_aslr_base();

#ifdef __cplusplus
}
#endif
