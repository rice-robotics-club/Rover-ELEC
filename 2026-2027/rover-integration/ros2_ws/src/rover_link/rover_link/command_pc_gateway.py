#!/usr/bin/env python3
"""Command-station gateway: ROS 2 motor command  ->  RFD900x radio A.

Subscribes: /station/motor_cmd (std_msgs/Bool)   True = motor on, False = off
Publishes:  /station/link_ok   (std_msgs/Bool)   True while the rover is acking
Params:     port, baud, send_hz, cmd_timeout, link_timeout

Safety: the last command is re-sent at send_hz (the rover's watchdog needs a
heartbeat), and if no command arrives from the control node for cmd_timeout
seconds the gateway sends OFF by itself.
"""
import time

import rclpy
import serial
from rclpy.node import Node
from std_msgs.msg import Bool

from rover_link.protocol import LineDecoder, encode
from rover_link.steady import steady_timer

STALL_S = 0.3   # a timer/callback that runs this much later than expected is logged
SLOW_S = 0.05   # a serial write or publish slower than this is logged


class CommandPCGateway(Node):
    def __init__(self):
        super().__init__('command_pc_gateway')
        self.declare_parameter('port', '/dev/ttyUSB0') # overriden by --ros-args -p port:=...
        self.declare_parameter('baud', 57600)
        self.declare_parameter('send_hz', 10.0)
        self.declare_parameter('cmd_timeout', 1.0)
        self.declare_parameter('link_timeout', 2.0)

        port = self.get_parameter('port').value
        baud = int(self.get_parameter('baud').value)
        self.cmd_timeout = float(self.get_parameter('cmd_timeout').value)
        self.link_timeout = float(self.get_parameter('link_timeout').value)

        # Raises if the radio is missing: better than running with no link.
        self.ser = serial.Serial(port, baud, timeout=0)
        self.decoder = LineDecoder()

        self.motor_on = False
        self.last_cmd_time = None    # monotonic (only goes up) time of last control message
        self.last_ack_time = None    # monotonic time of last ack (acknowledgment) from rover
        self.link_ok = False
        self._ticks = {}             # callback name -> monotonic time of last run

        self.create_subscription(Bool, '/station/motor_cmd', self.on_cmd, 10) #make a subscription to /station/motor_cmd
        self.link_pub = self.create_publisher(Bool, '/station/link_ok', 10) #make a publisher for if the link is good
        steady_timer(self, 1.0 / float(self.get_parameter('send_hz').value), #heartbeat 10hz
                     self.send_state)
        steady_timer(self, 0.02, self.read_radio) #to read the radio every 20ms
        self.get_logger().info(f'Radio A open on {port} @ {baud} baud')

    # ---- diagnostics ----------------------------------------------------
    def _tick_check(self, name):
        """Warn if a callback runs much later than its period (executor stall)."""
        now = time.monotonic()
        last = self._ticks.get(name)
        self._ticks[name] = now
        if last is not None and now - last > STALL_S:
            self.get_logger().warn(f'STALL: {name} did not run for {now - last:.2f} s')

    # ---- outgoing -------------------------------------------------------
    def on_cmd(self, msg: Bool): #the callback function in the subscriber 
        self._tick_check('on_cmd') #check for a stall
        self.last_cmd_time = time.monotonic()
        if msg.data != self.motor_on: # if its different (motor_on could be false) 
            self.motor_on = msg.data # update it
            self.get_logger().info(f'Command: motor {"ON" if msg.data else "OFF"}')
            self.send_state()  # don't wait for the next heartbeat

    def send_state(self):
        self._tick_check('send_state')
        now = time.monotonic()
        if self.motor_on and (self.last_cmd_time is None
                              or now - self.last_cmd_time > self.cmd_timeout):
            self.motor_on = False # turn off motor if hasnt gotten an On yet or if its been the timeout (who cares)
            self.get_logger().warn('Control node went quiet: sending motor OFF')
        self._write(encode(t='motor', v=int(self.motor_on))) #pass in one newline-terminated JSON packet with 0 or 1 to next function

    #now we have {"t":"motor","v":1}\n  or v=0

    def _write(self, packet: bytes):
        t0 = time.monotonic()
        try:
            self.ser.write(packet) #serial write the packet which looks like {"t":"motor","v":1}\n literally
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio write failed: {e}',
                                    throttle_duration_sec=2.0)
        took = time.monotonic() - t0
        if took > SLOW_S:
            self.get_logger().warn(f'SLOW: serial write took {took:.2f} s')

    # ---- incoming -------------------------------------------------------
    def read_radio(self):
        self._tick_check('read_radio')
        try:
            data = self.ser.read(256) #serial read up to 256 bytes
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio read failed: {e}',
                                    throttle_duration_sec=2.0)
            data = b''
        for msg in self.decoder.feed(data): #look at incoming msgs, which look like {"t":"ack","v":1,"ok":1}\n for an ack
            if msg.get('t') == 'ack': #dictionary lookup
                self.last_ack_time = time.monotonic()
        self._update_link()

    def _update_link(self):
        ok = (self.last_ack_time is not None
              and time.monotonic() - self.last_ack_time < self.link_timeout)
        if ok != self.link_ok: #ok could be false, if it is or if its now true update
            self.link_ok = ok
            self.get_logger().info('Rover link UP' if ok else 'Rover link LOST')
        t0 = time.monotonic()
        self.link_pub.publish(Bool(data=self.link_ok)) #publish the boolean of if the links good (local, not over the radio)
        took = time.monotonic() - t0
        if took > SLOW_S:
            self.get_logger().warn(f'SLOW: ROS publish took {took:.2f} s')

    def close(self):
        for _ in range(3):  # best effort: leave the rover stopped
            self._write(encode(t='motor', v=0))
        self.ser.close()


def main(args=None):
    rclpy.init(args=args)
    node = CommandPCGateway()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
