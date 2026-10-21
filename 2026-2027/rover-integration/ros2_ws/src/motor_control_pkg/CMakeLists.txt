cmake_minimum_required(VERSION 3.8)
project(motor_control_pkg)

find_package(ament_cmake REQUIRED)
find_package(rclpy REQUIRED)

install(PROGRAMS
  src/spi_motor_node.py
  src/spi_motor_node_off.py
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()