# Fix Debug Log Download API

**Status**: Backlog
**Priority**: Medium
**Created**: 2025-12-03

## Problem

The HTTP API endpoint `GET /api/logs/download` does not work - returns no data or connection fails. This prevents remote debugging and log access when the device is mounted/inaccessible.

## Root Cause (To Investigate)

Possible issues:
1. Log file not being flushed to disk before download
2. File handle locking issues
3. Chunked transfer encoding problems
4. HTTP timeout during large file reads
5. LVGL port locking conflicts during file access

## Proposed Solution

1. **Debug the endpoint** - Add extensive logging to identify failure point
2. **Test file access** - Verify log file is readable outside of HTTP context
3. **Fix chunked transfer** - Ensure proper HTTP streaming implementation
4. **Add error handling** - Return meaningful error responses
5. **Test with large logs** - Verify works with multi-MB log files

## Testing

- [ ] Verify log file exists and has content
- [ ] Test direct file read outside HTTP handler
- [ ] Test HTTP download with small log (<1KB)
- [ ] Test HTTP download with large log (>1MB)
- [ ] Verify no memory leaks during streaming
- [ ] Test concurrent access (UI + HTTP)

## Success Metrics

- Log download succeeds via HTTP API
- Works with logs up to 10 MB
- No crashes or hangs
- Proper error messages on failure
