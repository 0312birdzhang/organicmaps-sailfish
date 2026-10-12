project(live_transit_tests)

set(SRC
  departures_tests.cpp
  gtfs_realtime_tests.cpp
  trafiklab_tests.cpp
  transitous_tests.cpp
  vehicle_motion_tests.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} PRIVATE live_transit)
