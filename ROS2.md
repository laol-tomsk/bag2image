# bag2image на ROS 2 Jazzy

Пакет читает **только rosbag2**: каталог с `metadata.yaml` (в том числе запись,
разбитую на несколько файлов) либо отдельный `.db3`/`.mcap`. Старые ROS 1 `.bag`
не поддерживаются. Для сжатия поддерживается сжатие внутри MCAP; внешнее
сжатие rosbag2 в режиме `file`/`message` здесь не реализовано.

Сохранены названия 13 утилит, позиционные аргументы, формат TSV и имена PNG.
Вместо `rosrun bag2image ...` используется `ros2 run bag2image ...`.
Эти программы работают с файлами без ROS-узлов и без DDS discovery.
Выходной каталог для изображений нужно создать заранее.

```bash
mkdir -p images
ros2 run bag2image extract_images ./recording /camera/image_raw ./images
ros2 run bag2image extract_imu ./recording /imu > imu.tsv
ros2 run bag2image extract_gps --nostamp ./recording /fix > gps.tsv
ros2 run bag2image exclude_child_frame ./recording ./filtered origin
ros2 launch bag2image extract_images.launch file:=./recording topic:=/camera/image_raw directory:=./images
```

`exclude_child_frame` создаёт **новый каталог**, который ещё не должен существовать.
Формат хранилища наследуется от входа. В `/tf` и `/tf_static` удаляются только
трансформации с заданным `child_frame_id`; пустые TF-сообщения не записываются.
Прочие сообщения копируются без десериализации. Сохраняются метаданные топиков,
включая QoS и определения типов, а также временные метки сообщений хранилища.
Выходная запись получает собственный `metadata.yaml` с пересчитанными счётчиками.

В TSV с заголовком используется `header.stamp`, у `Float64` и `UInt32` — время
приёма из записи. `extract_gps --nostamp` также выбирает время приёма.
`extract_pc2` сохраняет прежний набор полей `x,y,xq,yq,zq,wq,dist` типа `FLOAT64`.

## Проверка

Из корня проекта, команды выполняет пользователь:

```bash
bash ros2_ws/src/total/scripts/run.sh bag2image
bash ros2_ws/src/total/scripts/run.sh all
```

Дополнительные зависимости в общем Docker-образе не понадобились; пересобирать
его для этого переноса не требуется. `test/test_tools.py` создаёт записи SQLite3
и MCAP и запускает установленные бинарники. Проверяются все форматы TSV,
разница времени заголовка и записи, пиксели raw/compressed изображений,
облако точек, разделённая запись, фильтрация TF, сохранение данных и QoS,
пустой результат фильтра и установленный launch-файл.

Общий прогон включает сценарий в `total/test/test_chains.py`: реальные статические
TF-публикаторы из `ntr_tf_tools` по схеме `navigation.launch` → запись MCAP →
фильтр `bag2image` → `ntr_topic_tools/rosbag_repeater` → поздний подписчик и TF2.

## Отдельный контейнерный разбор

`parse_bag.sh` и `parser.dockerfile` переведены на сборку **этой локальной копии**
под Jazzy; загрузка ROS 1 исходников с GitHub больше не используется.
Это отдельный образ, он не нужен для тестирования пакета в общем окружении.
Запускать вручную из каталога с записью; результаты сохраняются там же:

```bash
bash /path/to/bag2image/parse_bag.sh
```

Команды разбора остаются непосредственно в `ENTRYPOINT` файла `parser.dockerfile`.
Сохранены четыре прежних топика `/android/tango1/...`, результаты `camera-*`,
`imu-*.tsv`, `gps-*.tsv`, `compass-*.tsv` и изменение прав через `chmod`.
Как в исходном варианте, запись выбирается первым элементом списка содержимого
рабочего каталога. Теперь это может быть каталог rosbag2 либо файл `.db3`/`.mcap`;
расширение `.bag` к пути не добавляется.

API чтения и записи сверены с исходниками Jazzy:
[Reader](https://github.com/ros2/rosbag2/blob/jazzy/rosbag2_cpp/include/rosbag2_cpp/reader.hpp),
[Writer](https://github.com/ros2/rosbag2/blob/jazzy/rosbag2_cpp/include/rosbag2_cpp/writer.hpp).
