*** Settings ***
Resource        ${RENODEKEYWORDS}
Suite Setup     Setup
Suite Teardown  Teardown
Test Teardown   Test Teardown

*** Test Cases ***
Monitor Starts
    ${help}=  Execute Command  help
    Should Contain  ${help}  Available commands

# Future test after bms.elf exists:
# BMS Firmware Boots
#     Execute Command  include @../bms_stm32f103_bq76940.resc
#     Execute Command  start
#     Wait For Line On Uart  BMS boot
