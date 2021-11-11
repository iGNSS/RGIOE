//
// Created by rebeater on 2020/12/17.
//
#include "Define.h"
#include "NavStruct.h"
#include "FileIO.h"
#include "Config.h"
#include "DataFusion.h"
#include "NavLog.h"
#include "Timer.h"
#include <Alignment.h>

void navExit(const std::string &info) {
  loge << info;
  abort();
}
/**
 * 移动文件指针到开始时间，需要重载各种运算符
 * @tparam T 格式
 * @param is 文件流
 * @param t 数据存储位置
 * @param gpst 要挪到的时间
 */
template<class T>
void moveFilePoint(ifstream &is, T &t, double gpst) {
  do {
	is >> t;
  } while (t.gpst < gpst and is.good());
  is >> t;
}

int main(int argc, char *argv[]) {
  logInit(argv[0], "./");
  cout << CopyRight;
  if (argc < 2) {
	loge << CopyRight << endl;
	return 1;
  }
  Config cfg(argv[1]);
  Option opt = cfg.getOption();

  logi << "IMU path:" << cfg.imu_filepath;
  logi << "IMU rate:" << opt.d_rate;
  logi << " imu para:\n" << opt.imuPara;
  logi << "gnss path:" << cfg.gnss_filepath;

  ImuData imu;
  GnssData gnss;
  NavOutput out;
  AuxiliaryData aux;
  IMUSmooth smooth;

/*移动文件指针到指定的开始时间*/
  ifstream f_imu(cfg.imu_filepath);
  moveFilePoint(f_imu, imu, cfg.start_time);
  ifstream f_gnss(cfg.gnss_filepath);
  moveFilePoint(f_gnss, gnss, cfg.start_time);
  ifstream f_odo(cfg.odo_filepath);
  moveFilePoint(f_odo, aux, cfg.start_time);
  LOG_IF(ERROR, !f_odo.good()) << "odometer file open failed";
  NavWriter writer(cfg.output_filepath);
  logi << cfg.getImuPara();
  /*初始对准*/
  NavEpoch nav;
  if (opt.align_mode == AlignMode::ALIGN_MOVING) {
	logi << "Align moving mode, wait for GNSS";
	AlignMoving align{1.3, opt};
	do {
	  readImu(f_imu, &imu, cfg.imu_format);
	  align.Update(imu);
	  if (fabs(gnss.gpst - imu.gpst) < 1. / opt.d_rate) {
		logi << gnss.gpst << " velocity = " << align.Update(gnss);
		f_gnss >> gnss;
	  }
	} while (!align.alignFinished() and f_imu.good() and f_gnss.good());
	if (!align.alignFinished()) {
//	  navExit("align failed");
	  return 1;
	}
	nav = align.getNavEpoch();
  } else if (opt.align_mode == ALIGN_USE_GIVEN) {
	auto nav_ = cfg.getInitNav();
	nav = makeNavEpoch(nav_, opt);/* 这是UseGiven模式对准 */
  } else {
	logf << "supported align mode" << opt.align_mode;
	return 1;
  }
  Timer timer;
  DataFusion::Instance().Initialize(nav, opt);
  ofstream of_imu(cfg.imu_filepath + "smoothed.txt");
  logi << "initial PVA:" << DataFusion::Instance().Output();
  moveFilePoint(f_odo, aux, imu.gpst);
  /* loop function 1: end time <= 0 or 0  < imu.gpst < end time */
  while ((cfg.end_time <= 0) || (cfg.end_time > 0 && imu.gpst < cfg.end_time)) {
	f_imu >> imu;
	if (!f_imu.good())break;
	DataFusion::Instance().TimeUpdate(imu);
	smooth.Update(imu);
	of_imu << smooth.getSmoothedIMU() << ' ' << smooth.getStd() << ' ' << smooth.isStatic() << '\n';
	if (f_gnss.good() and fabs(gnss.gpst - imu.gpst) < 1.0 / opt.d_rate) {
	  DataFusion::Instance().MeasureUpdatePos(gnss);
	  LOG_EVERY_N(INFO, 100) << "GNSS update:" << gnss;
	  f_gnss >> gnss;
	}
	if (opt.odo_enable and f_odo.good() and fabs(aux.gpst - imu.gpst) < 1.0 / opt.d_rate) {
	  DataFusion::Instance().MeasureUpdateVel(aux.velocity);
	  LOG_EVERY_N(INFO, 50 * 100) << "Odo update:" << aux;
	  do { f_odo >> aux; } while (aux.gpst < imu.gpst and f_odo.good());
	}
	out = DataFusion::Instance().Output();
	writer.update(out);
  }
  /*show summary and reports:*/
  double time_resolve = static_cast<double>(timer.elapsed()) / 1000.0;
  writer.stop();
  double time_writing = static_cast<double> (timer.elapsed()) / 1000.0;
  f_imu.close();
  f_gnss.close();
  f_odo.close();
  logi << "\n\tSummary:\n"
	   << "\tAll epochs:" << DataFusion::Instance().EpochCounter() << '\n'
	   << "\tTime for Computing:" << time_resolve << "s" << '\n'
	   << "\tTime for File Writing:" << time_writing << 's' << '\n'
	   << "\tFinal PVA:" << DataFusion::Instance().Output() << '\n'
	   << "\tFinal Scale Factor:" << out.kd;
  return 0;
}

