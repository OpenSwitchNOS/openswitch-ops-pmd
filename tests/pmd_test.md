# pmd Test Cases

[TOC]

## Test Initial Conditions
### Objective
Verify that interfaces' pm\_info initially has connector=absent and connector\_status=unrecognized
### Requirements
 - Virtual Mininet Test Setup
### Setup
#### Topology Diagram
```
[s1]
```
### Description ###
1. Get list of interfaces
2. Verify that interfaces list is not empty
3. For each interface:
  1. verify that if the pm\_info data is present
  2. verify pm\_info "connector" is "absent"
  3. verify pm\_info "connector\_status" is unrecognized
### Test Result Criteria
#### Test Pass Criteria
All verifications succeed.
#### Test Fail Criteria
One or more verifications fail.

## Test Insertion and Removal of SFP Modules
### Objective
Verify that insertion and deletion is detected and EEPROM data is parsed.
### Requirements
 - Virtual Mininet Test Setup
### Setup
#### Topology Diagram
```
[s1]
```
### Description
1. Select an SFP interface
2. Verify that pm\_info "connector" is "absent" and "connector\_status" is "unrecognized"
3. For each SFP test data set
  1. Simulate insertion of module
  2. Verify that data in pm\_info matches expected data
  3. Simulate removal of module
  4. Verify that pm\_info "connector" is "absent" and "connector\_status" is "unrecognized"
  5. Verify that no other values are in pm\_info
### Test Result Criteria
#### Test Pass Criteria
All verifications succeed.
#### Test Fail Criteria
One or more verifications fail.

## Test Insertion and Removal of QSFP Modules
### Objective
Verify that insertion and deletion is detected and EEPROM data is parsed.
### Requirements
 - Virtual Mininet Test Setup
### Setup
#### Topology Diagram
```
[s1]
```
### Description
1. Select an QSFP interface
2. Verify that pm\_info "connector" is "absent" and "connector\_status" is "unrecognized"
3. For each QSFP test data set
  1. Simulate insertion of module
  2. Verify that data in pm\_info matches expected data
  3. Simulate removal of module
  4. Verify that pm\_info "connector" is "absent" and "connector\_status" is "unrecognized"
  5. Verify that no other values are in pm\_info
### Test Result Criteria
#### Test Pass Criteria
All verifications succeed.
#### Test Fail Criteria
One or more verifications fail.
