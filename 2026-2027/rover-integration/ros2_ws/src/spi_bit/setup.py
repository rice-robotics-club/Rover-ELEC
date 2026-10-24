from setuptools import find_packages, setup

package_name = 'spi_bit'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='user',
    maintainer_email='sw202@rice.edu',
    description='Output a 0 or 1 over SPI on a Jetson Orin Nano',
    license='Apache-2.0',
    entry_points={
        'console_scripts': [
            'spi_bit_node = spi_bit.spi_bit_node:main',
        ],
    },
)
