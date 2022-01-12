/**
* @file config.h in UiDataFusion
* @author rebeater
* @comment
* Create on 11/24/21 10:25 AM
* @version 1.0
**/

#ifndef UIDATAFUSION__CONFIG_H_
#define UIDATAFUSION__CONFIG_H_
#include "NavStruct.h"
#include <string>
class BaseConfig {
 public:
  bool enable = false;
  std::string file_path;
};
struct GnssConfig : public BaseConfig {
  GnssFileFormat format{GNSS_TXT_POS_7};
  int columns = 7;
  int index[9]{0};/*存储列标*/
  bool double_antenna_enable{0};
  float level_arm[3]{0};
  float pitch_of_antenna{0};
  float yaw_of_antenna{0};
};


class IMUConfig : public BaseConfig {
 public:
  IMUFileFormat format{IMU_FILE_IMUTXT};
  IMUFrame frame{IMU_FRAME_FRD};
  int d_rate{0};
  std::string parameter_path;
  ImuPara para{0};
};

class OdometerConfig : public BaseConfig {
 public:
  float odometer_std{0};
  bool nhc_enable{false};
  float nhc_std[2]{0};
  float angle_bv[3]{0};
  float wheel_level_arm[3]{0};
  float scale_factor{0};
  float scale_factor_std;
};

class ZuptConfig {
 public:
  bool zupt_enable{false};
  bool zupta_enable{false};
  float zupt_std{0};
  float zupta_std{0};
  int zupt_window{0};
  int static_wide{0};
  float threshold{0};
};

class AlignConfig {
 public:
  AlignMode mode{ALIGN_MOVING};
  float vel_threshold_for_moving{0};
  NavOutput init_pva{0};
};

class OutageConfig {
 public:
  bool enable = false;
  float start{0};
  float stop{0};
  float step{0};
  float outage{0};

};
class Config {
 public:
  bool LoadImuPara(std::string &error_msg);
 public:
  float start_time{0};
  float stop_time{0};
  std::string output_path;
  GnssConfig gnss_config;
  IMUConfig imu_config;
  OdometerConfig odometer_config;
  ZuptConfig zupt_config;
  AlignConfig align_config;
  OutageConfig outage_config;
  void SaveTo(const std::string &path) const;
  std::string ToStdString()const;
  void LoadFrom(const std::string &path);
  Option GetOption() const;
};

#endif //UIDATAFUSION__CONFIG_H_
