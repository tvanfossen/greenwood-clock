/**
 * @file settings_mock.h
 * @version 1
 * @brief An in-memory settings backend with sample data (see settings_mock.c).
 */
#pragma once

#include "settings_backend.h"
#include "sched_json.h"

/**
 * @brief The sample backend.
 * @version 1
 * @req REQ-TOOL-004
 * @return It (static).
 */
const settings_backend_t *settings_mock_backend(void);
