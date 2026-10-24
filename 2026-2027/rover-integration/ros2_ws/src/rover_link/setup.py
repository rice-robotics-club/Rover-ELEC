from setuptools import find_packages, setup

package_name = 'rover_link'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools', 'pyserial'],
    zip_safe=True,
    maintainer='user',
    maintainer_email='sw202@rice.edu',
    description='RFD900x radio link between the command station and the rover',
    license='Apache-2.0',
    entry_points={
        'console_scripts': [
            'command_pc_gateway = rover_link.command_pc_gateway:main',
            'rover_gateway = rover_link.rover_gateway:main',
            'keyboard_motor = rover_link.keyboard_motor:main',
            'topic_bridge = rover_link.topic_bridge:main',
            'demo_talker = rover_link.demo_talker:main',
            'demo_echo = rover_link.demo_echo:main',
        ],
    },
)
