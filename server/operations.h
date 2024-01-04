#ifndef SERVER_OPERATIONS_H
#define SERVER_OPERATIONS_H

#include <stddef.h>

/// Initializes the EMS state.
/// @param delay_us Delay in microseconds.
/// @return 0 if the EMS state was initialized successfully, 1 otherwise.
int ems_init(unsigned int delay_us);

/// Destroys the EMS state.
int ems_terminate();

/// Creates a new event with the given id and dimensions.
/// @param event_id Id of the event to be created.
/// @param num_rows Number of rows of the event to be created.
/// @param num_cols Number of columns of the event to be created.
/// @return 0 if the event was created successfully, 1 otherwise.
int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols);

/// Creates a new reservation for the given event.
/// @param event_id Id of the event to create a reservation for.
/// @param num_seats Number of seats to reserve.
/// @param xs Array of rows of the seats to reserve.
/// @param ys Array of columns of the seats to reserve.
/// @return 0 if the reservation was created successfully, 1 otherwise.
int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys);

/// Prepares information about given event. This list must NOT be freed by the caller.
/// @param event_id Id of the event to print.
/// @param num_rows Pointer to number of rows of the event to print.
/// @param num_cols Pointer to number of columns of the event to print.
/// @param data Pointer to array of seats of the event to print.
/// @return 0 if the event was printed successfully, 1 otherwise.
int ems_show(unsigned int event_id, size_t* num_rows, size_t* num_cols, unsigned int** data);

/// Prepares a list of all the events. This list MUST be freed by the caller.
/// @param num_events Pointer to number of events.
/// @param event_ids Pointer to array of event ids.
/// @return 0 if the events were printed successfully, 1 otherwise.
/// @warning event_ids MUST be freed by the caller.
int ems_list_events(size_t* num_events, unsigned int** event_ids);

#endif  // SERVER_OPERATIONS_H
