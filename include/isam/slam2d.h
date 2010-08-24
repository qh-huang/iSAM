/**
 * @file slam2d.h
 * @brief Provides specialized nodes and factors for 2D SLAM.
 * @author Michael Kaess
 * @version $Id: slam2d.h 2898 2010-08-24 01:06:18Z kaess $
 *
 * Copyright (C) 2009-2010 Massachusetts Institute of Technology.
 * Michael Kaess (kaess@mit.edu) and John J. Leonard (jleonard@mit.edu)
 *
 * This file is part of iSAM.
 *
 * iSAM is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * iSAM is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with iSAM.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <string>
#include <sstream>

#include "Node.h"
#include "Factor.h"
#include "Pose2d.h"
#include "Point2d.h"

namespace isam {


typedef NodeT<Pose2d> Pose2d_Node;
typedef NodeT<Point2d> Point2d_Node;

/**
 * Convert upper triangular square root information matrix to string.
 * @param sqrtinf Upper triangular square matrix.
 */
inline std::string sqrtinf_to_string(const Matrix& sqrtinf) {
  int nrows = sqrtinf.num_rows();
  int ncols = sqrtinf.num_cols();
  require(nrows==ncols, "slam2d::sqrtinf_to_string: matrix must be square");
  std::stringstream s;
  s << "{";
  bool first = true;
  for (int r=0; r<nrows; r++) {
    for (int c=r; c<ncols; c++) {
      if (first) {
        first = false;
      } else {
        s << ",";
      }
      s << sqrtinf(r,c);
    }
  }
  s << "}";
  return s.str();
}


/**
 * Prior on Point2d.
 */
class Point2d_Factor : public Factor {
public:
  const Point2d prior;
  /**
   * Constructor.
   * @param point The point node the prior acts on.
   * @param prior The actual prior measurement.
   * @param sqrtinf The 2x2 square root information matrix (upper triangular).
   */
  Point2d_Factor(Point2d_Node* point, const Point2d& prior, const Matrix& sqrtinf)
  : Factor("Point2d_Factor", 2, sqrtinf), prior(prior) {
    _nodes.resize(1);
    _nodes[0] = point;
  }
  void initialize() {
    Point2d_Node* point = dynamic_cast<Point2d_Node*>(_nodes[0]);
    if (!point->initialized()) {
      Point2d predict = prior;
      point->init(predict);
    }
  }
  Vector basic_error(const std::vector<Vector>& vec) const {
    return (vec[0] - prior.vector());
  }
  void write(std::ostream &out) const {
    Factor::write(out);
    out << " " << prior << " " << sqrtinf_to_string(_sqrtinf);
  }
};

/**
 * Prior on Pose2d.
 */
class Pose2d_Factor : public Factor {
public:
  const Pose2d prior;
  /**
   * Constructor.
   * @param pose The pose node the prior acts on.
   * @param prior The actual prior measurement.
   * @param sqrtinf The 3x3 square root information matrix (upper triangular).
   */
  Pose2d_Factor(Pose2d_Node* pose, const Pose2d& prior, const Matrix& sqrtinf)
  : Factor("Pose2d_Factor", 3, sqrtinf), prior(prior) {
    _nodes.resize(1);
    _nodes[0] = pose;
  }
  void initialize() {
    Pose2d_Node* pose = dynamic_cast<Pose2d_Node*>(_nodes[0]);
    if (!pose->initialized()) {
      Pose2d predict = prior;
      pose->init(predict);
    }
  }
  Vector basic_error(const std::vector<Vector>& vec) const {
    Vector err = vec[0] - prior.vector();
    err(2) = standardRad(err(2));
    return err;
  }
  Jacobian jacobian() {
    Matrix M = _sqrtinf; // derivatives are all 1 (eye)
    Vector err = _nodes[0]->vector0() - prior.vector();
    err(2) = standardRad(err(2));
    Vector r = _sqrtinf * err;
    Jacobian jac(r);
    jac.add_term(_nodes[0], M);
    return jac;
  }
  void write(std::ostream &out) const {
    Factor::write(out);
    out << " " << prior << " " << sqrtinf_to_string(_sqrtinf);
  }
};

/**
 * Odometry or loop closing constraint, from pose1 to pose2.
 */
class Pose2d_Pose2d_Factor : public Factor {
public:
  const Pose2d measure;
  /**
   * Constructor.
   * @param pose1 The pose from which the measurement starts.
   * @param pose2 The pose to which the measurement extends.
   * @param measure The relative measurement from pose1 to pose2 (pose2 in pose1's frame).
   * @param sqrtinf The 3x3 square root information matrix (upper triangular).
   * @param anchor1 Optional anchor node for trajectory to which pose1 belongs to.
   * @param anchor2 Optional anchor node for trajectory to which pose2 belongs to.
   */
  Pose2d_Pose2d_Factor(Pose2d_Node* pose1, Pose2d_Node* pose2,
      const Pose2d& measure, const Matrix& sqrtinf,
      Pose2d_Node* anchor1 = NULL, Pose2d_Node* anchor2 = NULL)
  : Factor("Pose2d_Pose2d_Factor", 3, sqrtinf), measure(measure) {
    require((anchor1==NULL && anchor2==NULL) || (anchor1!=NULL && anchor2!=NULL),
        "slam2d: Pose2d_Pose2d_Factor requires either 0 or 2 anchor nodes");
    if (anchor1) { // offset between two relative pose graphs
      _nodes.resize(4);
      _nodes[2] = anchor1;
      _nodes[3] = anchor2;
    } else {
      _nodes.resize(2);
    }
    _nodes[0] = pose1;
    _nodes[1] = pose2;
  }
  void initialize() {
    Pose2d_Node* pose1 = dynamic_cast<Pose2d_Node*>(_nodes[0]);
    Pose2d_Node* pose2 = dynamic_cast<Pose2d_Node*>(_nodes[1]);
    require(pose1->initialized(),
        "slam2d: Pose2d_Pose2d_Factor requires pose1 to be initialized");
    if (!pose2->initialized()) {
      Pose2d a = pose1->value();
      Pose2d predict = a.oplus(measure);
      pose2->init(predict);
    }
    if (_nodes.size()==4) {
      Pose2d_Node* anchor1 = dynamic_cast<Pose2d_Node*>(_nodes[2]);
      Pose2d_Node* anchor2 = dynamic_cast<Pose2d_Node*>(_nodes[3]);
      require(anchor1->initialized(),
          "slam2d: Pose2d_Pose2d_Factor requires anchor1 to be initialized");
      if (!anchor2->initialized()) {
        Pose2d a = pose1->value();
        Pose2d b = pose2->value();
        Pose2d b1 = anchor1->value();
        Pose2d d = measure.ominus(b.ominus(b1.oplus(a)));
        anchor2->init(d);
      }
    }
  }
  Vector basic_error(const std::vector<Vector>& vec) const {
    Pose2d p1(vec[0]);
    Pose2d p2(vec[1]);
    Pose2d predicted;
    if (vec.size()==4) {
      Pose2d anchor1(vec[2]);
      Pose2d anchor2(vec[3]);
      predicted = (anchor2.oplus(p2)).ominus(anchor1.oplus(p1));
    } else {
      Pose2d p = p2.ominus(p1);
      predicted = p.vector();
    }
    Vector err = predicted.vector() - measure.vector();
    err(2) = standardRad(err(2));
    return err;
  }
  Jacobian jacobian() {
    if (_nodes.size()==4) { // symbolic available below only without anchor nodes
      return Factor::jacobian();
    } else {
      Pose2d_Node* pose1 = dynamic_cast<Pose2d_Node*>(_nodes[0]);
      Pose2d_Node* pose2 = dynamic_cast<Pose2d_Node*>(_nodes[1]);
      Pose2d p1 = pose1->value0();
      Pose2d p2 = pose2->value0();
      Pose2d p = p2.ominus(p1);
      double c = cos(p1.t());
      double s = sin(p1.t());
      Matrix M1 = make_Matrix(3, 3,
          -c, -s,  p.y(),
          s,  -c,  -p.x(),
          0.,  0., -1.
      );
      M1 = _sqrtinf * M1;
      Matrix M2 = make_Matrix(3, 3,
          c,   s,   0.,
          -s,  c,   0.,
          0.,  0.,  1.
      );
      M2 = _sqrtinf * M2;
      Vector err = p.vector() - measure.vector();
      err(2) = standardRad(err(2));
      Vector r = _sqrtinf * err;
      Jacobian jac(r);
      jac.add_term(pose1, M1);
      jac.add_term(pose2, M2);
      return jac;
    }
  }
  void write(std::ostream &out) const {
    Factor::write(out);
    out << " " << measure << " " << sqrtinf_to_string(_sqrtinf);
    if (_nodes.size()==4) {
      out << " " << _nodes[2]->unique_id() << " " << _nodes[3]->unique_id();
    }
  }
};

/**
 * Landmark observation.
 */
class Pose2d_Point2d_Factor : public Factor {
public:
  const Point2d measure;
  /**
   * Constructor.
   * @param pose The pose from which the landmark is observed.
   * @param point The point or landmark that is observed
   * @param measure The relative observation of the landmark in the pose's frame.
   * @param sqrtinf The 2x2 square root information matrix (upper triangular).
   */
  Pose2d_Point2d_Factor(Pose2d_Node* pose, Point2d_Node* point,
      const Point2d& measure, const Matrix& sqrtinf)
  : Factor("Pose2d_Point2d_Factor", 2, sqrtinf), measure(measure) {
    _nodes.resize(2);
    _nodes[0] = pose;
    _nodes[1] = point;
  }
  void initialize() {
    Pose2d_Node* pose = dynamic_cast<Pose2d_Node*>(_nodes[0]);
    Point2d_Node* point = dynamic_cast<Point2d_Node*>(_nodes[1]);
    require(pose->initialized(),
        "slam2d: Pose2d_Point2d_Factor requires pose to be initialized");
    if (!point->initialized()) {
      Pose2d p = pose->value();
      Point2d predict = p.transform_from(measure);
      point->init(predict);
    }
  }
  Vector basic_error(const std::vector<Vector>& vec) const {
    Pose2d po(vec[0]);
    Point2d pt(vec[1]);
    Point2d p = po.transform_to(pt);
    Vector predicted = p.vector();
    return (predicted - measure.vector());
  }
  Jacobian jacobian() {
    Pose2d_Node* pose = dynamic_cast<Pose2d_Node*>(_nodes[0]);
    Point2d_Node* point = dynamic_cast<Point2d_Node*>(_nodes[1]);
    Pose2d po = pose->value0();
    Point2d pt = point->value0();
    double c = cos(po.t());
    double s = sin(po.t());
    double dx = pt.x() - po.x();
    double dy = pt.y() - po.y();
    // f(x)
    double x =  c*dx + s*dy; // relative forward position of landmark point from pose
    double y = -s*dx + c*dy; // relative position to the left
    Matrix M1 = make_Matrix(2, 3,
        -c, -s,  y,
        s,  -c, -x
    );
    M1 = _sqrtinf * M1;
    Matrix M2 = make_Matrix(2, 2,
        c,   s,
        -s,  c
    );
    M2 = _sqrtinf * M2;
    Point2d p(x, y);
    Vector r = _sqrtinf * (p.vector() - measure.vector());
    Jacobian jac(r);
    jac.add_term(pose, M1);
    jac.add_term(point, M2);
    return jac;
  }
  void write(std::ostream &out) const {
    Factor::write(out);
    out << " " << measure << " " << sqrtinf_to_string(_sqrtinf);
  }
};

}