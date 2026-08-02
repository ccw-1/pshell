# pshell TODO List

## Expect Buffer Feature - Completed ✅

- [x] Implement dedicated expect buffer architecture
- [x] Add dual-write system (raw buffer + expect buffer)
- [x] Implement buffer seeding from screen content
- [x] Integrate with expect engine (activate/deactivate)
- [x] Add line position tracking
- [x] Implement socket protocol handler
- [x] Fix pshellctl client JSON parsing
- [x] Test pattern matching functionality
- [x] Document implementation
- [x] Create maintenance guide

## Known Issues & Future Improvements

### High Priority

- [ ] **Buffer Size Management**
  - Current: Fixed 64KB expect buffer
  - Issue: May lose old data if buffer fills
  - Solution: Implement dynamic buffer expansion or configurable size
  - Impact: Medium - affects long-running expect operations

- [ ] **Line Position Accuracy**
  - Current: Counts newlines in buffer
  - Issue: VT100 escape sequences affect line counting
  - Solution: Use screen buffer line tracking instead
  - Impact: Low - line position is informational

- [ ] **Multiple Concurrent Expects**
  - Current: One expect per session
  - Issue: Cannot run multiple expect operations simultaneously
  - Solution: Per-context buffers or operation queuing
  - Impact: Medium - limits automation scenarios

### Medium Priority

- [ ] **Pattern Matching Performance**
  - Current: Linear search O(n*m)
  - Issue: Slow for large buffers (64KB)
  - Solution: Implement Boyer-Moore or incremental matching
  - Impact: Low - only affects large buffer searches

- [ ] **Memory Management**
  - Current: Expect buffer kept until session destruction
  - Issue: Memory not freed after expect completes
  - Solution: Free buffer after inactivity period
  - Impact: Low - 64KB per session is acceptable

- [ ] **Timeout Precision**
  - Current: 10ms polling with usleep()
  - Issue: Not precise, wastes CPU
  - Solution: Use select/poll for event-driven timeout
  - Impact: Low - current implementation works

### Low Priority

- [ ] **Screen Buffer Seeding**
  - Current: May include VT100 escape sequences
  - Issue: Pattern matching against escape sequences
  - Solution: Strip escape sequences or use plain text
  - Impact: Very Low - rarely causes issues

- [ ] **Error Reporting**
  - Current: Limited error codes
  - Issue: Hard to diagnose failures
  - Solution: Add detailed error codes and messages
  - Impact: Low - debug logging usually sufficient

- [ ] **Test Suite**
  - Current: Manual testing only
  - Issue: No automated regression tests
  - Solution: Create comprehensive test suite
  - Impact: Medium - important for maintenance

## Feature Requests

- [ ] **Expect Multiple Patterns**
  - Wait for any of N patterns to match
  - Return which pattern matched
  - Use case: Branching automation logic

- [ ] **Expect with Callback**
  - Register callback for pattern match
  - Async notification instead of blocking
  - Use case: Event-driven automation

- [ ] **Pattern History**
  - Track all matched patterns in session
  - Query match history
  - Use case: Debugging, audit trail

- [ ] **Expect Statistics**
  - Track match count, average time, failures
  - Per-session and global stats
  - Use case: Performance monitoring

- [ ] **Buffer Snapshots**
  - Save expect buffer state at intervals
  - Replay/analyze later
  - Use case: Debugging complex scenarios

## Documentation

- [x] EXPECT_BUFFER_IMPLEMENTATION.md - Technical details
- [x] MAINTENANCE.md - Maintenance guide
- [x] TODO.md - This file
- [ ] API_REFERENCE.md - Complete API documentation
- [ ] EXAMPLES.md - Usage examples and patterns
- [ ] TROUBLESHOOTING.md - Common issues and solutions

## Testing

- [x] Basic pattern matching
- [x] Line position tracking
- [x] Historical pattern matching
- [ ] Timeout behavior
- [ ] Buffer overflow handling
- [ ] Multiple sessions
- [ ] Concurrent operations
- [ ] Memory leak testing (valgrind)
- [ ] Performance benchmarks
- [ ] Edge cases (empty buffer, huge patterns, etc.)

## Build & Release

- [x] Makefile builds all binaries
- [x] No compilation warnings
- [ ] Static analysis (cppcheck, clang-tidy)
- [ ] Code coverage analysis
- [ ] Performance profiling
- [ ] Release packaging
- [ ] Installation script

## Notes

- Expect buffer implementation completed 2026-08-02
- All core functionality working and tested
- Focus next on test suite and documentation
- Consider performance improvements for v2.0
