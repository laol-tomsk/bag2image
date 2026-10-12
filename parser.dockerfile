FROM ros:jazzy-ros-base-noble

RUN bash -c "mkdir /tmp/share"

RUN bash -c " \
    mkdir -p ~/ros2_ws/src && \
    source /opt/ros/jazzy/setup.bash && \
    cd ~/ros2_ws/src \
"

RUN bash -c " \
    apt update && \
    apt install -y git \
"

COPY package.xml CMakeLists.txt /root/ros2_ws/src/bag2image/
COPY src /root/ros2_ws/src/bag2image/src
COPY launch /root/ros2_ws/src/bag2image/launch

RUN apt-get update && apt-get install -y --no-install-recommends python3-colcon-common-extensions \
    && rm -rf /var/lib/apt/lists/*

RUN bash -c " \
    cd ~/ros2_ws/src && \
    source /opt/ros/jazzy/setup.bash && \
    apt-get update && \
    rosdep install --from-paths -y . --ignore-src && \
    cd ~/ros2_ws && \
    colcon build --cmake-args -DBUILD_TESTING=OFF \
"

ENTRYPOINT \
    bash -c " \
        source /opt/ros/jazzy/setup.bash && \
        source ~/ros2_ws/install/setup.bash && \
        bagpath=\`ls -1 /tmp/share | head -n 1\` && \
        bagname=\${bagpath%.*} && \
        cameradir=camera-\${bagname%.*} && \
        mkdir -p /tmp/share/\$cameradir && \
        ros2 run bag2image extract_images /tmp/share/\$bagpath /android/tango1/camera_1/image/compressed /tmp/share/\$cameradir && \
        chmod -R go+rw /tmp/share/\$cameradir && \
        imufile=imu-\$bagname.tsv && \
        ros2 run bag2image extract_imu /tmp/share/\$bagpath /android/tango1/imu > /tmp/share/\$imufile && \
        chmod go+rw /tmp/share/\$imufile && \
        gpsfile=gps-\$bagname.tsv && \
        ros2 run bag2image extract_gps --nostamp /tmp/share/\$bagpath /android/tango1/fix > /tmp/share/\$gpsfile && \
        chmod go+rw /tmp/share/\$gpsfile && \
        magfile=compass-\$bagname.tsv && \
        ros2 run bag2image extract_compass /tmp/share/\$bagpath /android/tango1/magnetic_field > /tmp/share/\$magfile && \
        chmod go+rw /tmp/share/\$magfile \
    "