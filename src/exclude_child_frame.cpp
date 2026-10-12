#include <algorithm>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <tf2_msgs/msg/tf_message.hpp>
#include <boost/program_options.hpp>
#include <iostream>


namespace po = boost::program_options;


constexpr char OPT_HELP[] = "help";
constexpr char OPT_INBAG[] = "inbag";
constexpr char OPT_OUTBAG[] = "outbag";
constexpr char OPT_FRAME[] = "frame";


int main(int argc, char** argv) {
    const po::variables_map vm = [] (const int argc, char** argv) -> const po::variables_map {
        po::options_description arguments("Arguments");
        arguments.add_options()
            (OPT_INBAG, po::value<std::string>(), "Input rosbag2 directory or storage file")
            (OPT_OUTBAG, po::value<std::string>(), "Output rosbag2 directory (must not exist)")
            (OPT_FRAME, po::value<std::string>(), "Frame to exclude")
        ;

        po::positional_options_description arguments_pos;
        arguments_pos.add(OPT_INBAG, 1);
        arguments_pos.add(OPT_OUTBAG, 1);
        arguments_pos.add(OPT_FRAME, 1);

        po::options_description general("General options");
        general.add_options()
            (OPT_HELP, "print this message")
        ;

        po::options_description all_options("All options");
        all_options.add(arguments).add(general);

        po::variables_map vm;
        po::store(
            po::command_line_parser(argc, argv).options(all_options).positional(arguments_pos).run(),
            vm
        );
        po::notify(vm);

        if (
                vm.count(OPT_HELP) != 0 ||
                vm.count(OPT_INBAG) != 1 ||
                vm.count(OPT_OUTBAG) != 1 ||
                vm.count(OPT_FRAME) != 1
        ) {
            std::cout << "exclude_child_frame [options]";
            std::cout << ' ' << OPT_INBAG;
            std::cout << ' ' << OPT_OUTBAG;
            std::cout << ' ' << OPT_FRAME;
            std::cout << std::endl << std::endl;
            std::cout << arguments << std::endl;
            std::cout << general << std::endl;
            exit(1);
        }

        return vm;
    } (argc, argv);

    rosbag2_cpp::Reader inbag;
    inbag.open(vm[OPT_INBAG].as<std::string>());
    rosbag2_cpp::Writer outbag;
    rosbag2_storage::StorageOptions options;
    options.uri = vm[OPT_OUTBAG].as<std::string>();
    options.storage_id = inbag.get_metadata().storage_identifier;
    outbag.open(options);
    std::vector<rosbag2_storage::MessageDefinition> definitions;
    inbag.get_all_message_definitions(definitions);
    for (const auto& topic : inbag.get_all_topics_and_types()) {
        const auto definition = std::find_if(definitions.begin(), definitions.end(),
            [&topic] (const auto& entry) { return entry.topic_type == topic.type; });
        if (definition != definitions.end())
            outbag.create_topic(topic, *definition);
        else
            outbag.create_topic(topic);
    }
    const std::string exclude = vm[OPT_FRAME].as<std::string>();

    while (inbag.has_next()) {
        const auto instance = inbag.read_next();
        const auto& topic = instance->topic_name;
        if (topic == "/tf" || topic == "/tf_static") {
            const auto msg = std::make_shared<tf2_msgs::msg::TFMessage>();
            rclcpp::SerializedMessage serialized(*instance->serialized_data);
            rclcpp::Serialization<tf2_msgs::msg::TFMessage> serialization;
            serialization.deserialize_message(&serialized, msg.get());
            msg->transforms.erase(
                std::remove_if(
                    msg->transforms.begin(),
                    msg->transforms.end(),
                    [&exclude] (const geometry_msgs::msg::TransformStamped& tf) {
                        return (tf.child_frame_id == exclude);
                    }
                ),
                msg->transforms.end()
            );
            if (msg->transforms.size() > 0) {
                const auto filtered = std::make_shared<rclcpp::SerializedMessage>();
                serialization.serialize_message(msg.get(), filtered.get());
                outbag.write(
                    filtered,
                    topic,
                    "tf2_msgs/msg/TFMessage",
                    instance->recv_timestamp,
                    instance->send_timestamp
                );
            }
        } else {
            outbag.write(instance);
        }
    }
    outbag.close();

    return 0;
}
