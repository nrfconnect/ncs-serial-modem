/* AT monitors */
. = ALIGN(4);
_sm_monitor_entry_list_start = .;
KEEP(*(SORT_BY_NAME("._sm_monitor_entry.*")));
_sm_monitor_entry_list_end = .;
