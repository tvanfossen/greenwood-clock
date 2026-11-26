/**
 * @file settings_mock.h
 * v2: settings_mock_library_fill.
 * @version 2
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

/**
 * @brief Fill @p be's Library calls with the sample ones (settings_mock_library.c).
 * @version 1
 * @req REQ-LIB-001
 */
void settings_mock_library_fill(settings_backend_t *be);


