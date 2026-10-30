"""Timers that run on a steady (monotonic) clock instead of ROS/system time.

A normal rclpy timer follows ROS time, which is the system wall clock. If the
wall clock is stepped backwards (time sync, WSL2 clock correction), such a
timer stays silent until the clock catches up again, even though subscription
callbacks keep running. A steady-clock timer ignores wall-clock changes.
"""
from rclpy.clock import Clock, ClockType


def steady_timer(node, period_s, callback):
    """Create a node timer driven by the steady clock."""
    try:
        return node.create_timer(
            period_s, callback, clock=Clock(clock_type=ClockType.STEADY_TIME))
    except TypeError:  # an rclpy without the clock argument
        node.get_logger().warn(
            'create_timer has no clock argument here; using ROS time instead')
        return node.create_timer(period_s, callback)
