"""Run installed utilities on real rosbag2 recordings with known contents."""
from pathlib import Path
import struct
import subprocess

from ament_index_python.packages import get_package_prefix
import cv2
from geographic_msgs.msg import GeoPointStamped
from geometry_msgs.msg import PoseStamped, PoseWithCovarianceStamped, TransformStamped, TwistStamped
from nav_msgs.msg import Odometry
import numpy as np
import pytest
from rclpy.serialization import deserialize_message, serialize_message
import rosbag2_py as bag
from rosbag2_py._storage import QoS
from sensor_msgs.msg import CompressedImage, Image, Imu, MagneticField, NavSatFix, PointCloud2, PointField
from std_msgs.msg import Float64, UInt32
from tf2_msgs.msg import TFMessage
import yaml

STORAGES = ('sqlite3', 'mcap')
KINDS = ('imu', 'gps', 'compass', 'float64', 'uint32', 'posestamped',
         'geopointstamped', 'odometry', 'twiststamped', 'posewithcovariancestamped')
POSE_HEADER = 'pos_x\tpos_y\tpos_z\tquat_x\tquat_y\tquat_z\tquat_w'
TWIST_HEADER = 'lin_x\tlin_y\tlin_z\tang_x\tang_y\tang_z'
POSE = ['1.25', '-2.5', '3.75', '0', '0', '0', '1']
TWIST = ['4', '5', '6', '-7', '-8', '-9']
COV9 = ','.join(str(i) for i in range(9))
COV36 = ','.join(str(i) for i in range(36))
EXPECTED = {
    'imu': ('quat_x\tquat_y\tquat_z\tquat_w\taccel_x\taccel_y\taccel_z\tangular_x\tangular_y\tangular_z',
            ['0', '0', '0', '1', '4', '5', '6', '-7', '-8', '-9']),
    'gps': ('lat\tlon\talt\tcovariance', ['55.125', '83.25', '123.5', COV9]),
    'compass': ('mag_x\tmag_y\tmag_z\tcovariance', ['4', '5', '6', COV9]),
    'float64': ('data', ['-1.25']),
    'uint32': ('data', ['4294967295']),
    'posestamped': (POSE_HEADER, POSE),
    'geopointstamped': ('lat\tlon\talt', ['55.125', '83.25', '123.5']),
    'odometry': (POSE_HEADER + '\tpose_cov\t' + TWIST_HEADER + '\ttwist_cov',
                 POSE + [COV36] + TWIST + [COV36]),
    'twiststamped': (TWIST_HEADER, TWIST),
    'posewithcovariancestamped': (POSE_HEADER + '\tpose_cov', POSE + [COV36]),
}


def binary(name):
    path = Path(get_package_prefix('bag2image')) / 'lib/bag2image' / name
    assert path.is_file(), path
    return str(path)


def run(name, *args, code=0):
    result = subprocess.run([binary(name), *map(str, args)], capture_output=True, text=True, timeout=20)
    assert result.returncode == code, result.stdout + result.stderr
    return result.stdout


def typename(message):
    return type(message).__module__.split('.')[0] + '/msg/' + type(message).__name__


def writer(path, storage, topics):
    result = bag.SequentialWriter()
    result.open(bag.StorageOptions(uri=str(path), storage_id=storage), bag.ConverterOptions('cdr', 'cdr'))
    for i, (topic, message) in enumerate(topics.items()):
        qos = [QoS(1).reliable().transient_local()] if topic == '/tf_static' else []
        result.create_topic(bag.TopicMetadata(id=i, name=topic, type=typename(message),
                                             serialization_format='cdr', offered_qos_profiles=qos))
    return result


def read(path):
    reader = bag.SequentialReader()
    reader.open(bag.StorageOptions(uri=str(path)), bag.ConverterOptions('cdr', 'cdr'))
    messages = []
    while reader.has_next():
        messages.append(reader.read_next())
    reader.close()
    return messages


def samples():
    pose = PoseStamped()
    pose.pose.position.x, pose.pose.position.y, pose.pose.position.z = 1.25, -2.5, 3.75
    pose.pose.orientation.w = 1.0
    twist = TwistStamped()
    twist.twist.linear.x, twist.twist.linear.y, twist.twist.linear.z = 4.0, 5.0, 6.0
    twist.twist.angular.x, twist.twist.angular.y, twist.twist.angular.z = -7.0, -8.0, -9.0
    pose_cov = PoseWithCovarianceStamped()
    pose_cov.pose.pose = pose.pose
    pose_cov.pose.covariance = [float(i) for i in range(36)]
    odometry = Odometry()
    odometry.pose = pose_cov.pose
    odometry.twist.twist = twist.twist
    odometry.twist.covariance = [float(i) for i in range(36)]
    imu = Imu(orientation=pose.pose.orientation, linear_acceleration=twist.twist.linear,
              angular_velocity=twist.twist.angular)
    gps = NavSatFix(latitude=55.125, longitude=83.25, altitude=123.5,
                    position_covariance=[float(i) for i in range(9)])
    compass = MagneticField(magnetic_field=twist.twist.linear,
                            magnetic_field_covariance=[float(i) for i in range(9)])
    geo = GeoPointStamped()
    geo.position.latitude, geo.position.longitude, geo.position.altitude = 55.125, 83.25, 123.5
    return dict(zip(KINDS, (imu, gps, compass, Float64(data=-1.25), UInt32(data=4294967295),
                            pose, geo, odometry, twist, pose_cov)))


@pytest.fixture(scope='module', params=STORAGES)
def recording(request, tmp_path_factory):
    storage = request.param
    path = tmp_path_factory.mktemp(storage) / 'recording'
    messages = samples()
    output = writer(path, storage, {'/' + key: value for key, value in messages.items()})
    for i in range(2):
        for key, message in messages.items():
            if hasattr(message, 'header'):
                message.header.stamp.sec = 12 + i
                message.header.stamp.nanosec = 345
            output.write('/' + key, serialize_message(message), (90 + i) * 10**9 + 678)
    output.close()
    return path


@pytest.mark.parametrize('kind', KINDS)
def test_tsv(recording, kind):
    lines = run('extract_' + kind, recording, '/' + kind).splitlines()
    header, values = EXPECTED[kind]
    seconds, nanos = (90, 678) if kind in ('float64', 'uint32') else (12, 345)
    expected = ['nsec\t' + header]
    expected += ['\t'.join([f'{seconds+i:09d}{nanos:09d}', *values]) for i in range(2)]
    assert lines == expected


def test_gps_bag_timestamp(recording):
    lines = run('extract_gps', '--nostamp', recording, '/gps').splitlines()
    assert [line.split('\t')[0] for line in lines[1:]] == ['000000090000000678', '000000091000000678']
    assert lines[1].split('\t')[1:] == EXPECTED['gps'][1]


def test_storage_file(recording):
    filename = next(path for path in recording.iterdir() if path.suffix in ('.db3', '.mcap'))
    assert run('extract_uint32', filename, '/uint32') == run('extract_uint32', recording, '/uint32')


@pytest.mark.parametrize('storage', STORAGES)
def test_split_recording(tmp_path, storage):
    path = tmp_path / 'split'
    output = writer(path, storage, {'/number': UInt32()})
    output.write('/number', serialize_message(UInt32(data=1)), 10**9)
    output.split_bagfile()
    output.write('/number', serialize_message(UInt32(data=2)), 2*10**9)
    output.close()
    assert run('extract_uint32', path, '/number').splitlines() == [
        'nsec\tdata', '000000001000000000\t1', '000000002000000000\t2']


def image_bag(path, storage, compressed):
    pixels = np.array([[[10, 20, 30], [40, 50, 60], [70, 80, 90]],
                       [[100, 110, 120], [130, 140, 150], [160, 170, 180]]], dtype=np.uint8)
    if compressed:
        ok, encoded = cv2.imencode('.png', pixels)
        assert ok
        message = CompressedImage(format='bgr8; png compressed bgr8', data=encoded.tobytes())
    else:
        message = Image(height=2, width=3, encoding='bgr8', step=9, data=pixels.tobytes())
    output = writer(path, storage, {'/camera': message, '/ignored': message})
    for i in range(2):
        message.header.stamp.sec = 12 + i
        message.header.stamp.nanosec = 345
        output.write('/camera', serialize_message(message), (90+i)*10**9)
    message.header.stamp.sec = 99
    output.write('/ignored', serialize_message(message), 99*10**9)
    output.close()
    return pixels


@pytest.mark.parametrize('storage', STORAGES)
@pytest.mark.parametrize('compressed', [False, True], ids=['raw', 'compressed'])
def test_images(tmp_path, storage, compressed):
    path, destination = tmp_path / 'bag', tmp_path / 'images'
    destination.mkdir()
    pixels = image_bag(path, storage, compressed)
    output = run('extract_images', path, '/camera', destination)
    assert '2 images extracted, 0 corrupted' in output
    files = sorted(destination.glob('*.png'))
    assert [p.name for p in files] == ['000000012000000345.png', '000000013000000345.png']
    for file in files:
        np.testing.assert_array_equal(cv2.imread(str(file)), pixels)


def test_images_missing_topic(recording, tmp_path):
    run('extract_images', recording, '/absent', tmp_path, code=1)
    assert not list(tmp_path.glob('*.png'))


def test_images_wrong_type(recording, tmp_path):
    output = run('extract_images', recording, '/imu', tmp_path, code=3)
    assert 'unsupported type sensor_msgs/msg/Imu' in output


@pytest.mark.parametrize('storage', STORAGES)
def test_pointcloud(tmp_path, storage):
    message = PointCloud2(height=1, width=2, is_bigendian=False, point_step=56, row_step=112)
    message.header.stamp.sec, message.header.stamp.nanosec = 12, 345
    message.fields = [PointField(name=name, offset=8*i, datatype=PointField.FLOAT64, count=1)
                      for i, name in enumerate(('x', 'y', 'xq', 'yq', 'zq', 'wq', 'dist'))]
    message.data = struct.pack('<14d', *range(1, 15))
    path = tmp_path / 'cloud'
    output = writer(path, storage, {'/cloud': message})
    output.write('/cloud', serialize_message(message), 90*10**9+678)
    output.close()
    lines = run('extract_pc2', path, '/cloud').splitlines()
    assert lines[0] == 'recieve\ttimestamp\tx\ty\txq\tyq\tzq\twq\tdist'
    assert lines[1:] == ['\t'.join(['000000090000000678', '000000012000000345',
                                   *map(str, range(start, start+7))]) for start in (1, 8)]


def transform(child):
    value = TransformStamped(child_frame_id=child)
    value.header.frame_id = 'world'
    value.header.stamp.sec = 12
    value.transform.translation.x = 1.25
    value.transform.rotation.w = 1.0
    return value


@pytest.mark.parametrize('storage', STORAGES)
def test_exclude_frame(tmp_path, storage):
    source, destination = tmp_path / 'source', tmp_path / 'filtered'
    topics = {'/tf': TFMessage(), '/tf_static': TFMessage(), '/number': UInt32()}
    output = writer(source, storage, topics)
    keep, remove = transform('keep'), transform('remove')
    for i, (topic, message) in enumerate([
        ('/tf', TFMessage(transforms=[remove, keep])),
        ('/tf_static', TFMessage(transforms=[keep, remove])),
        ('/tf', TFMessage(transforms=[remove])),
        ('/number', UInt32(data=123)),
        ('/tf', TFMessage(transforms=[keep])),
    ]):
        output.write(topic, serialize_message(message), 90*10**9+i)
    output.close()
    run('exclude_child_frame', source, destination, 'remove')
    result = read(destination)
    assert [(topic, stamp) for topic, _, stamp in result] == [
        ('/tf', 90*10**9), ('/tf_static', 90*10**9+1), ('/number', 90*10**9+3), ('/tf', 90*10**9+4)]
    for topic, data, _ in result:
        if topic in ('/tf', '/tf_static'):
            assert deserialize_message(data, TFMessage).transforms == [keep]
        else:
            assert data == serialize_message(UInt32(data=123))
    metadata = []
    for path in (source, destination):
        value = yaml.safe_load((path / 'metadata.yaml').read_text())['rosbag2_bagfile_information']
        assert value['storage_identifier'] == storage
        metadata.append({t['topic_metadata']['name']: t['topic_metadata']
                         for t in value['topics_with_message_count']})
    assert metadata[0] == metadata[1]
    assert metadata[1]['/tf_static']['offered_qos_profiles']


@pytest.mark.parametrize('storage', STORAGES)
def test_exclude_all_transforms(tmp_path, storage):
    source, destination = tmp_path / 'source', tmp_path / 'empty'
    output = writer(source, storage, {'/tf_static': TFMessage()})
    output.write('/tf_static', serialize_message(TFMessage(transforms=[transform('remove')])), 10**9)
    output.close()
    run('exclude_child_frame', source, destination, 'remove')
    assert read(destination) == []
    assert (destination / 'metadata.yaml').is_file()


@pytest.mark.parametrize('tool', ['extract_' + kind for kind in KINDS] +
                         ['extract_images', 'extract_pc2', 'exclude_child_frame'])
def test_help(tool):
    output = run(tool, '--help', code=1 if tool in ('extract_pc2', 'exclude_child_frame') else 0)
    assert '--help' in output


def test_installed_launch(tmp_path):
    source, destination = tmp_path / 'source', tmp_path / 'images'
    destination.mkdir()
    pixels = image_bag(source, 'sqlite3', False)
    result = subprocess.run(['ros2', 'launch', 'bag2image', 'extract_images.launch',
                             'file:=' + str(source), 'topic:=/camera', 'directory:=' + str(destination)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    files = sorted(destination.glob('*.png'))
    assert len(files) == 2, result.stdout + result.stderr
    np.testing.assert_array_equal(cv2.imread(str(files[0])), pixels)
