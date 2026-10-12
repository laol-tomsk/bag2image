#include <rosbag2_cpp/reader.hpp>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/opencv.hpp>
#include <boost/program_options.hpp>


namespace po = boost::program_options;


constexpr char OPT_TOPIC[]     = "topic";
constexpr char OPT_DIRECTORY[] = "directory";
constexpr char OPT_BAG[]       = "bag";
constexpr char OPT_HELP[]      = "help";


int main(int argc, char **argv) {
    po::variables_map vm;
    {
        po::options_description pos_desc_backend("Arguments");
        pos_desc_backend.add_options()
            (OPT_BAG, po::value<std::string>(), "rosbag2 directory or storage file")
            (OPT_TOPIC, po::value<std::string>(), "topic name")
            (OPT_DIRECTORY, po::value<std::string>(), "output directory")
        ;

        po::positional_options_description pos_desc;
        pos_desc.add(OPT_BAG, 1);
        pos_desc.add(OPT_TOPIC, 1);
        pos_desc.add(OPT_DIRECTORY, 1);

        po::options_description vis_desc("Options");
        vis_desc.add_options()
            (OPT_HELP, "print this message")
        ;

        po::options_description all_desc("All options");
        all_desc.add(vis_desc).add(pos_desc_backend);

        po::store(
            po::command_line_parser(argc, argv).options(all_desc).positional(pos_desc).run(),
            vm
        );
        po::notify(vm);

        if (
                vm.count(OPT_HELP) ||
                !vm.count(OPT_BAG) ||
                !vm.count(OPT_TOPIC) ||
                !vm.count(OPT_DIRECTORY)
        ) {
            std::cout << "extract_images [options]";
            std::cout << " " << OPT_BAG;
            std::cout << " " << OPT_TOPIC;
            std::cout << " " << OPT_DIRECTORY;
            std::cout << std::endl << std::endl;
            std::cout << pos_desc_backend << std::endl;
            std::cout << vis_desc << std::endl;
            return 0;
        }
    }
    rosbag2_cpp::Reader bag;
    bag.open(vm[OPT_BAG].as<std::string>());
    std::vector<std::string> topics;
    const std::string topic_name = vm[OPT_TOPIC].as<std::string>();
    topics.emplace_back(topic_name);
    rosbag2_storage::StorageFilter view;
    view.topics = topics;
    bag.set_filter(view);
    std::string topic_type;
    uint64_t total = 0;
    for (const auto& topic : bag.get_metadata().topics_with_message_count) {
        if (topic.topic_metadata.name == topic_name) {
            topic_type = topic.topic_metadata.type;
            total = topic.message_count;
        }
    }
    if (total == 0) {
        std::cerr << "There is no [" << topic_name << "] topic in " << vm[OPT_BAG].as<std::string>() << std::endl;
        return 1;
    }
    bool compressed;
    {
        if (topic_type == "sensor_msgs/msg/Image") {
            compressed = false;
        } else if (topic_type == "sensor_msgs/msg/CompressedImage") {
            compressed = true;
        } else {
            printf("Topic %s has unsupported type %s\n", topic_name.c_str(), topic_type.c_str());
            return 3;
        }
    }

    const double total_f = static_cast<double>(total);
    uint64_t corrupted = 0;
    uint64_t i = 0;
    const std::string output_dir = vm[OPT_DIRECTORY].as<std::string>();
    while (bag.has_next()) {
        const auto instance = bag.read_next();
        i++;
        if (compressed) {
            auto msg = std::make_shared<sensor_msgs::msg::CompressedImage>();
            rclcpp::SerializedMessage serialized(*instance->serialized_data);
            rclcpp::Serialization<sensor_msgs::msg::CompressedImage> serialization;
            serialization.deserialize_message(&serialized, msg.get());
            if (msg != NULL) {
                cv_bridge::CvImagePtr cv_image = cv_bridge::toCvCopy(msg);
                std::ostringstream ss_sec, ss_nsec;
                ss_sec << std::setw(9) << std::setfill('0') << msg->header.stamp.sec;
                ss_nsec << std::setw(9) << std::setfill('0') << msg->header.stamp.nanosec;
                cv::imwrite(output_dir + "/" + ss_sec.str() + ss_nsec.str() + ".png", cv_image->image);
            } else {
                printf("Corrupted message\n");
                corrupted++;
                continue;
            }
        } else {
            auto msg = std::make_shared<sensor_msgs::msg::Image>();
            rclcpp::SerializedMessage serialized(*instance->serialized_data);
            rclcpp::Serialization<sensor_msgs::msg::Image> serialization;
            serialization.deserialize_message(&serialized, msg.get());
            if (msg != NULL) {
                cv_bridge::CvImagePtr cv_image = cv_bridge::toCvCopy(msg);
                std::ostringstream ss_sec, ss_nsec;
                ss_sec << std::setw(9) << std::setfill('0') << msg->header.stamp.sec;
                ss_nsec << std::setw(9) << std::setfill('0') << msg->header.stamp.nanosec;
                cv::imwrite(output_dir + "/" + ss_sec.str() + ss_nsec.str() + ".png", cv_image->image);
            } else {
                printf("Corrupted message\n");
                corrupted++;
                continue;
            }
        }
        printf("\r%lu/%lu (%3.1f%%)", i, total, static_cast<float>(i) / static_cast<float>(total) * 100.f);
    }
    printf("\n%lu images extracted, %lu corrupted\n", total - corrupted, corrupted);

    bag.close();
    return 0;
}
