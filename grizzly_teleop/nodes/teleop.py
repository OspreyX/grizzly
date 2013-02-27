#!/usr/bin/python

""" If we're using the usual Logitech gamepad, suggest using "X" mode
via the switch on the front. That will map the buttons to "Green=GO, 
Red=E-Stop"""

import roslib; roslib.load_manifest('grizzly_teleop')
import rospy

from sensor_msgs.msg import Joy
from geometry_msgs.msg import Twist
from std_msgs.msg import Bool

class Teleop:
    def __init__(self):
        rospy.init_node('grizzly_teleop')

        self.turn_scale = rospy.get_param('~turn_scale')
        self.drive_scale = rospy.get_param('~drive_scale')
        self.slow_scale = rospy.get_param('~slow_scale',10.0)
        self.deadman_button = rospy.get_param('~deadman_button', 0)
        self.fast_button = rospy.get_param('~fast_button', 1)
        self.estop_button = rospy.get_param('~estop_button', 2)
        self.estop_button2 = rospy.get_param('~estop_button2', 3)

        self.cmd = None
        cmd_pub = rospy.Publisher('cmd_vel', Twist)
        self.estop = False
        cmd_estop = rospy.Publisher('teleop_estop', Bool)

        rospy.Subscriber("joy", Joy, self.callback)
        rate = rospy.Rate(rospy.get_param('~hz', 20))
        
        while not rospy.is_shutdown():
            rate.sleep()
            if self.cmd:
                cmd_pub.publish(self.cmd)
            """ As long as we're active, give it e-stop data
            A manual reset is required once an e-stop is asserted
            anyway, and having to reset everytime we let go of the
            joystick isn't the point """
            cmd_estop.publish(self.estop)
        

    def callback(self, data):
        """ Receive joystick data, formulate Twist message. """
        cmd = Twist()

        if data.buttons[self.deadman_button] == 1:
            cmd.linear.x = data.axes[1] * self.drive_scale / self.slow_scale
            cmd.angular.z = data.axes[0] * self.turn_scale / self.slow_scale
            self.cmd = cmd
        # Only allow fast motion when the regular deadman isn't pressed
        # Pressing both will result in slow motion
        elif data.buttons[self.fast_button] == 1:
            cmd.linear.x = data.axes[1] * self.drive_scale
            cmd.angular.z = data.axes[0] * self.turn_scale 
            self.cmd = cmd
        else:
            self.cmd = None
        if data.buttons[self.estop_button] == 1 or data.buttons[self.estop_button2] == 1:
            self.estop = True
        else:
            self.estop = False


if __name__ == "__main__": 
    Teleop() 
