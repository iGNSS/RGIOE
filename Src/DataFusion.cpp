//
// Created by rebeater on 2020/12/17.
//

#include "DataFusion.h"
#include "navigation_log.h"
#include <iomanip>
//DataFusion(NavEpoch &ini_nav,Option opt):ins(ini_nav,opt.d_rate),KalmanFilter()
/**
 * 初始化P,Q矩阵
 * @param ini_nav
 * @param opt
 */
DataFusion::DataFusion(NavEpoch &ini_nav, Option &opt) : Ins(ini_nav, opt.d_rate), KalmanFilter(),opt(opt) {
    /*initial P & Q0 */
    P.setZero();
    P.block<3, 3>(0, 0) = ini_nav.pos_std.asDiagonal();
    P.block<3, 3>(3, 3) = ini_nav.vel_std.asDiagonal();
    P.block<3, 3>(6, 6) = ini_nav.att_std.asDiagonal();
    Vec3d temp = Vec3d{opt.imuPara.gb_std[0], opt.imuPara.gb_std[1], opt.imuPara.gb_std[2]};
    P.block<3, 3>(9, 9) = temp.asDiagonal();
    temp = Vec3d{opt.imuPara.ab_std[0], opt.imuPara.ab_std[1], opt.imuPara.ab_std[2]};
    P.block<3, 3>(12, 12) = temp.asDiagonal();
    P = P * P;/*计算协方差矩阵*/
    logi << "P=\n" << P.diagonal().transpose();
    Q0.setZero();
    Q0(3, 3) = opt.imuPara.vrw * opt.imuPara.vrw;
    Q0(4, 4) = opt.imuPara.vrw * opt.imuPara.vrw;
    Q0(5, 5) = opt.imuPara.vrw * opt.imuPara.vrw;
    Q0(6, 6) = opt.imuPara.arw * opt.imuPara.arw;
    Q0(7, 7) = opt.imuPara.arw * opt.imuPara.arw;
    Q0(8, 8) = opt.imuPara.arw * opt.imuPara.arw;

    Q0(9, 9) = 2 * opt.imuPara.gb_std[0] * opt.imuPara.gb_std[0] / opt.imuPara.gt_corr;
    Q0(10, 10) = 2 * opt.imuPara.gb_std[1] * opt.imuPara.gb_std[1] / opt.imuPara.gt_corr;
    Q0(11, 11) = 2 * opt.imuPara.gb_std[2] * opt.imuPara.gb_std[2] / opt.imuPara.gt_corr;

    Q0(12, 12) = 2 * opt.imuPara.ab_std[0] * opt.imuPara.ab_std[0] / opt.imuPara.at_corr;
    Q0(13, 13) = 2 * opt.imuPara.ab_std[1] * opt.imuPara.ab_std[1] / opt.imuPara.at_corr;
    Q0(14, 14) = 2 * opt.imuPara.ab_std[2] * opt.imuPara.ab_std[2] / opt.imuPara.at_corr;
    logi << "Q0=\n" << Q0.diagonal().transpose();
    lb_gnss = Vec3d{opt.lb_gnss[0], opt.lb_gnss[1], opt.lb_gnss[2]};
    logi << "initial finished";
}

/**
 * time update
 * @param imu : imu data
 * @return : 1 success 0 fail in time check
 */
int DataFusion::TimeUpdate(ImuData &imu) {
    ForwardMechanization(imu);
    MatXd phi = TransferMatrix(opt.imuPara);
//    LOG_EVERY_N(INFO,10)<<dt;
    LOG_IF(WARNING, fabs(dt - 1.0/opt.d_rate)>0.001 ) << "dt error" << dt;
    MatXd Q = 0.5 * (phi * Q0 + Q0 * phi.transpose()) * dt;
//    MatXd Q = 0.5 * (phi * Q0 * phi.transpose() + Q0) * dt;
    Predict(phi, Q);
    return 0;
}

/**
 * Gnss Position Measure Update
 * @param pos
 * @param Rk
 * @return 1
 */
int DataFusion::MeasureUpdatePos(Vec3d &pos, Mat3d &Rk) {
    Mat3Xd H = _pos_h();
    Vec3d z = _pos_z(pos);
    Update(H, z, Rk);
    _feed_back();
    Reset();
    return 0;
}

int DataFusion::MeasureUpdatePos(GnssData &gnssData) {
    Vec3d pos(gnssData.lat * _deg, gnssData.lon * _deg, gnssData.height);
    Mat3d Rk = Mat3d::Zero();
    Rk(0, 0) = gnssData.pos_std[0] * gnssData.pos_std[0];
    Rk(1, 1) = gnssData.pos_std[1] * gnssData.pos_std[1];
    Rk(2, 2) = gnssData.pos_std[2] * gnssData.pos_std[2];
    MeasureUpdatePos(pos, Rk);
    return 0;
}


/**
 * feed back Modified Error Models
 * @return
 */
int DataFusion::_feed_back() {
    /* TODO */
    double lat = nav.pos[0];
    double h = nav.pos[2];
    double rn = wgs84.RN(lat);
    double rm = wgs84.RM(lat);
    Vec3d d_atti = Vec3d{xd[1] / (rn + h),
                         -xd[0] / (rm + h),
                         -xd[1] * tan(lat) / (rn + h)
    };
    Vec3d _d_atti = Vec3d{-xd[1] / (rn + h),
                          +xd[0] / (rm + h),
                          +xd[1] * tan(lat) / (rn + h)
    };;
    Quad qnc = convert::rv_to_quaternion(_d_atti);
    nav.Qne = (nav.Qne * qnc).normalized();
    LatLon ll = convert::qne_to_lla(nav.Qne);
    nav.pos[0] = ll.latitude;
    nav.pos[1] = ll.longitude;
    nav.pos[2] = nav.pos[2] + xd[2];
    Mat3d Ccn = eye3 + convert::skew(d_atti);
//    LOG_FIRST_N(INFO,10)<<xd.block<3, 1>(3, 0).transpose();
    nav.vn = Ccn * (nav.vn - Vec3d{xd[3], xd[4], xd[5]});
    Vec3d phi = Vec3d{xd[6], xd[7], xd[8]} + d_atti;
    Quad Qpn = convert::rv_to_quaternion(phi);
    nav.Qbn = (Qpn * nav.Qbn).normalized();
    nav.Cbn = convert::quaternion_to_dcm(nav.Qbn);
    nav.atti = convert::dcm_to_euler(nav.Cbn);
    nav.gb += Vec3d{xd[9], xd[10], xd[11]};
    nav.ab += Vec3d{xd[12], xd[13], xd[14]};

//    LOG_EVERY_N(INFO,10)<<nav.gb.transpose();
//    LOG_EVERY_N(INFO,10)<<nav.ab.transpose();
    return 0;
}


Mat3Xd DataFusion::_pos_h() {
     Mat3Xd mat_h = Mat3Xd::Zero();
    mat_h.block<3, 3>(0, 0) = eye3;
    Vec3d temp = nav.Cbn * lb_gnss;
    mat_h.block<3, 3>(0, 6) = convert::skew(temp);
    return mat_h;
}

/**
 * pos measurement
 * @param pos
 * @return
 */
Vec3d DataFusion::_pos_z(Eigen::Vector3d &pos) {
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"gnss pos "<<pos.transpose();
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"ins pos "<<nav.pos.transpose();
//    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"pos "<<pos.transpose();
    Vec3d re_ins = convert::lla_to_xyz(nav.pos);
    Vec3d re_gnss = convert::lla_to_xyz(pos);
    LatLon gnss = {pos[0], pos[1]};
    Mat3d cne = convert::lla_to_cne(gnss);
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"re_ins "<<re_ins.transpose();

    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"re_gnss "<<re_gnss.transpose();
//    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"cne "<<cne*cne.transpose();
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"lb_gnss "<<lb_gnss.transpose();
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<" nav.Cbn * lb_gnss "<< (nav.Cbn * lb_gnss).transpose();
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"re_ins - re_gnss "<<(re_ins - re_gnss).transpose();
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"re_ins - re_gnss "<<(nav.Cne.transpose()* (re_ins - re_gnss)).transpose();

//    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"re "<<pos.transpose();
     Vec3d z = nav.Cne.transpose()* (re_ins - re_gnss) + nav.Cbn * lb_gnss;
    LOG_FIRST_N(INFO,10)<<std::setprecision(10)<<"z "<<z.transpose();
    return z;
}

NavOutput DataFusion::Output() {
    static NavOutput out;
    out.gpst = nav.gpst;
    for (int i = 0; i < 3; i++) {
        out.pos[i] = nav.pos[i];
        out.vn[i] = nav.vn[i];
        out.atti[i] = nav.atti[i];
        out.gb[i] = nav.gb[i];
        out.ab[i] = nav.ab[i];
    }
    return out;
}

