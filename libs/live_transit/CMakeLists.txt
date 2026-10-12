project(live_transit)

set(SRC
  departures.cpp
  departures.hpp
  gtfs_realtime.cpp
  gtfs_realtime.hpp
  journeys.hpp
  provider.hpp
  trafiklab.cpp
  trafiklab.hpp
  vehicle_motion.cpp
  vehicle_motion.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME}
  PUBLIC
    geometry

  PRIVATE
    base
    coding
    platform
    glaze::glaze
)

omim_add_test_subdirectory(live_transit_tests)
