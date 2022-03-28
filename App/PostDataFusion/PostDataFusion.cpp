//
// Created by rebeater on 2020/12/17.
//
#include "DataFusion.h"
#include "Alignment.h"
#include "NavStruct.h"
#include "FileIO.h"
#include "Config.h"
#include "NavLog.h"
#include "Timer.h"
#include "Outage.h"
#include "Smoother.h"
#include "fmt/format.h"

#include <list>

/*extern int GnssCheck(const GnssData &gnss){
  if (gnss.ns > 60) {
    return 0;
  }
  if (gnss.ns < 15) {//低于5的抛弃
    return 0;
  }
  if (gnss.mode == SPP) {
    return 1;
  }
  if (gnss.mode == RTK_DGPS) {
    return 2;
  } else if (gnss.mode == RTK_FLOAT || gnss.mode == RTK_FIX) {
    return 3;
  } else {
    return 0;
  }
}*/

/**
 * 移动文件指针到开始时间，需要重载各种运算符
 * @tparam T 格式
 * @param is 文件流
 * @param t 数据存储位置
 * @param gpst 要挪到的时间
 */

int main(int argc, char *argv[]) {
  logInit(argv[0], "./");
  cout << CopyRight;
  if (argc < 2) {
	loge << CopyRight << endl;
	return 1;
  }
  Config config;
  Smoother<double> odo_smooth{50};/*里程计平滑器*/
  config.LoadFrom(argv[1]);
  bool ok;
  string error_msg;
  ok = config.LoadImuPara(error_msg);
  Option opt = config.GetOption();
  LOG_IF(ERROR, !ok) << error_msg;
  LOG(INFO) << config.ToStdString();
  LOG(INFO) << opt.imuPara;
  if (config.odometer_config.enable)
	logi << "Odometer path:" << config.odometer_config.file_path;
  ImuData imu;
  GnssData gnss;
  NavOutput out;
  Velocity vel;
  PressureData  press;
  Outage outage_cfg{config.outage_config.start, config.outage_config.stop, config.outage_config.outage,
					config.outage_config.step};// = cfg.outage_config();
  LOG(INFO) << config.outage_config.start << " " << config.outage_config.stop << " " << config.outage_config.outage
			<< " " << config.outage_config.step;

  IMUReader imu_reader(config.imu_config.file_path,
					   config.imu_config.format,
					   config.imu_config.frame,
					   false, config.imu_config.d_rate);
  if (!imu_reader.IsOk()) {
	LOG(ERROR) << "No such file:" + config.imu_config.file_path;
	return 1;
  }
  if (!imu_reader.ReadUntil(config.start_time, &imu)) {
	LOG(ERROR) << "IMU data does NOT reach the start time: " << config.start_time;
	return 1;
  }
  GnssReader gnss_reader(config.gnss_config.file_path, config.gnss_config.format);
  if (!gnss_reader.IsOk()) {
	LOG(ERROR) << "No such file:" + config.gnss_config.file_path;
  }
  if (!gnss_reader.ReadUntil(imu.gpst, &gnss)) {
	LOG(WARNING) << "GNSS data does NOT reach the start time: " << config.start_time;
  }
  ReaderBase<Velocity> *podoReader = nullptr;//= new OdometerReader(config.odometer_config.file_path);
  if (config.odometer_config.enable) {
	LOG(INFO) << "Odometer path:" << config.odometer_config.file_path;
	podoReader = new OdometerReader(config.odometer_config.file_path);
	if (!podoReader->ReadUntil(imu.gpst, &vel)) {
	  LOG(ERROR) << "Error odometer data does NOT reach the start time";
	  return 1;
	}
  }
  BmpReader bmp_reader{config.pressure_config.file_path};
  if(config.pressure_config.enable and !bmp_reader.ReadUntil(config.start_time,&press)){
    LOG(ERROR) << "Error BMP280 data does NOT reach the start time";
    return 1;
  }

  NavWriter writer(config.output_path);
  NavEpoch nav;
  if (opt.align_mode == AlignMode::ALIGN_MOVING) {
	LOG(INFO) << "Align moving mode, wait for GNSS";
	AlignMoving align{config.align_config.vel_threshold_for_moving, opt};
	do {
	  imu_reader.ReadNext(imu);
	  align.Update(imu);
	  if (fabs(gnss.gpst - imu.gpst) < 1. / opt.d_rate) {
		logi << "aligning vel = " << align.Update(gnss);
		if (!gnss_reader.ReadNext(gnss)) {
		  LOG(ERROR) << "GNSS read finished,but align not complete!";
		  return 1;
		}
	  }
	} while (!align.alignFinished() and imu_reader.IsOk());
	if (!align.alignFinished()) {
	  LOG(ERROR) << "GNSS read finished,but align not complete!";
	  return 1;
	}
	nav = align.getNavEpoch();
  } else if (opt.align_mode == ALIGN_USE_GIVEN) {
	auto nav_ = config.align_config.init_pva;
	nav = makeNavEpoch(nav_, opt);/* 这是UseGiven模式对准 */
  } else {
	LOG(ERROR) << "supported align mode" << (int)opt.align_mode;
	return 1;
  }
  LOG(INFO) << "initial gyro bias:" << nav.gb.transpose() / _deg * _hour;

  Timer timer;
/*第一步：初始化*/
  DataFusion::Instance().Initialize(nav, opt);
  LOG(INFO) << "initial PVA:" << DataFusion::Instance().Output();
  if (opt.odo_enable) {
	podoReader->ReadUntil(imu.gpst, &vel);
  }
/* loop function 1: end time <= 0 or 0  < imu.gpst < end time */
  LOG(INFO) << "start:" << imu.gpst << ",end:" << config.stop_time;
//LOG(INFO) << "imu status:"<<imu_reader.IsOk()<<"\t gnss status:"<<gnss_reader.IsOk()<<"\t odo status:"<< podoReader->IsOk();
  while (((config.stop_time <= 0) || (config.start_time > 0 && imu.gpst < config.stop_time)) && imu_reader.IsOk()) {
	if (!imu_reader.ReadNext(imu))break;
	/*第二步 时间更新*/
	DataFusion::Instance().TimeUpdate(imu);
	/* GNSS更新 */
	if (gnss_reader.IsOk() and fabs(gnss.gpst - imu.gpst) < 0.6 / opt.d_rate) {
	  if (config.outage_config.enable and outage_cfg.IsOutage(gnss.gpst)) {
	    gnss.mode = GnssMode::INVALID;/*手动设置GNSS模式为INVALID*/
	  }
	  DataFusion::Instance().MeasureUpdatePos(gnss);
	   LOG_EVERY_N(INFO, 100) << "GNSS update:" << gnss<< "at "<<imu.gpst;
	  gnss_reader.ReadNext(gnss);
	  if (!gnss_reader.IsOk()) {
		LOG(WARNING) << "Gnss read failed" << gnss;
	  }
	}
	while (gnss.gpst < imu.gpst) {
	  if (!gnss_reader.ReadNext(gnss)) {
		LOG(WARNING) << "Gnss read failed" << gnss;
	  }
	}
	/*里程计更新*/
	if (opt.odo_enable and podoReader->IsOk() and fabs(vel.gpst - imu.gpst) < 1.0 / opt.d_rate) {
	  DataFusion::Instance().MeasureUpdateVel(odo_smooth.Update( vel.forward));
	  LOG_EVERY_N(INFO, 50 * 100) << "Odo update:" << vel.gpst;
	  podoReader->ReadNext(vel);
	  podoReader->ReadNext(vel);
	}
	if(config.pressure_config.enable and fabs(press.gpst-imu.gpst)<1.0/opt.d_rate){
	  double height = 44330*(1-pow(press.pressure/101325.0,0.19));
	  double z = DataFusion::Instance().MeasureUpdateRelativeHeight(height);
	  LOG(INFO)<<"pressure update: "<< z << "\t" <<height;
	  bmp_reader.ReadNext(press);
	}
	if (!opt.enable_rts) {
	  writer.update(DataFusion::Instance().Output());
	}
  }
  if (opt.enable_rts) {
	/* RTS模式下,输出结果顺序是反的,因此用栈结构存储 */
	LOG(INFO) << "Start RTS smooth,final state is "<<DataFusion::Instance().Output();
	LOG(INFO) << "State vector is "<<DataFusion::Instance().xd.transpose();
	bool finished;
	std::list<NavOutput> result;
	do {
	  finished = DataFusion::Instance().RtsUpdate();
	  out = DataFusion::Instance().Output();
	  result.push_back(out);
	  LOG_FIRST_N(INFO,1)<<out;
	  LOG_FIRST_N(INFO,1)<<DataFusion::Instance().xd.transpose();
	} while (!finished);
	LOG(INFO) << "Saving result...";
	while (!result.empty()) {
	  writer.update(result.back());
	  result.pop_back();
	}
	LOG(INFO) << "RTS smooth finished\n";
  }
  LOG(INFO) << "Process finished";
/*show summary and reports:*/
  double time_resolve = static_cast<double>(timer.elapsed()) / 1000.0;
  writer.stop();
  double time_writing = static_cast<double> (timer.elapsed()) / 1000.0;
  delete podoReader;
  LOG(INFO) << "\n\tSummary:\n"
			<< "\tAll epochs:" << DataFusion::Instance().EpochCounter() << '\n'
			<< "\tTime for Computing:" << time_resolve << "s" << '\n'
			<< "\tTime for File Writing:" << time_writing << 's' << '\n'
			<< "\tFinal PVA:" << DataFusion::Instance().Output() << '\n';
  LOG_IF(INFO, config.outage_config.enable)
		  << "outage:" << config.outage_config.outage << " s, from " << config.outage_config.start << " to "
		  << config.outage_config.stop;
	LOG(INFO)<<"GNSS level arm:"<<DataFusion::Instance().lb_gnss.transpose();
	LOG(INFO)<<"Odometer scale factor:"<<DataFusion::Instance().nav.kd;
  return 0;
}

