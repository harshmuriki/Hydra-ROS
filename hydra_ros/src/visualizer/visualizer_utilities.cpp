/* -----------------------------------------------------------------------------
 * Copyright 2022 Massachusetts Institute of Technology.
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Research was sponsored by the United States Air Force Research Laboratory and
 * the United States Air Force Artificial Intelligence Accelerator and was
 * accomplished under Cooperative Agreement Number FA8750-19-2-1000. The views
 * and conclusions contained in this document are those of the authors and should
 * not be interpreted as representing the official policies, either expressed or
 * implied, of the United States Air Force or the U.S. Government. The U.S.
 * Government is authorized to reproduce and distribute reprints for Government
 * purposes notwithstanding any copyright notation herein.
 * -------------------------------------------------------------------------- */
#include "hydra_ros/visualizer/visualizer_utilities.h"

#include <spark_dsg/node_attributes.h>
#include <tf2_eigen/tf2_eigen.h>

#include <random>

#include "hydra_ros/visualizer/colormap_utilities.h"

#include <string>
#include <cstdint>

#include <map>
#include <regex>



namespace hydra {

using dsg_utils::makeColorMsg;
using visualization_msgs::Marker;
using visualization_msgs::MarkerArray;

namespace {

inline double getRatio(double min, double max, double value) {
  double ratio = (value - min) / (max - min);
  ratio = !std::isfinite(ratio) ? 0.0 : ratio;
  ratio = ratio > 1.0 ? 1.0 : ratio;
  ratio = ratio < 0.0 ? 0.0 : ratio;
  return ratio;
}

inline void fillPoseWithIdentity(geometry_msgs::Pose& pose) {
  Eigen::Vector3d identity_pos = Eigen::Vector3d::Zero();
  tf2::convert(identity_pos, pose.position);
  tf2::convert(Eigen::Quaterniond::Identity(), pose.orientation);
}

}  // namespace

Color getDistanceColor(const VisualizerConfig& config,
                           const ColormapConfig& colors,
                           double distance) {
  if (config.places_colormap_max_distance <= config.places_colormap_min_distance) {
    // TODO(nathan) consider warning
    return Color();
  }

  double ratio = getRatio(config.places_colormap_min_distance,
                          config.places_colormap_max_distance,
                          distance);

  return dsg_utils::interpolateColorMap(colors, ratio);
}

Marker makeDeleteMarker(const std_msgs::Header& header,
                        size_t id,
                        const std::string& ns) {
  Marker marker;
  marker.header = header;
  marker.action = Marker::DELETE;
  marker.id = id;
  marker.ns = ns;
  return marker;
}

Marker makeLayerEllipseBoundaries(const std_msgs::Header& header,
                                  const LayerConfig& config,
                                  const SceneGraphLayer& layer,
                                  const VisualizerConfig& visualizer_config,
                                  const std::string& ns) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.id = 0;
  marker.ns = ns;
  marker.scale.x = config.boundary_wireframe_scale;
  fillPoseWithIdentity(marker.pose);
  marker.pose.position.z +=
      config.collapse_boundary ? 0.0 : getZOffset(config, visualizer_config);

  geometry_msgs::Point last_point;
  std_msgs::ColorRGBA color;

  for (const auto& id_node_pair : layer.nodes()) {
    const auto& attrs = id_node_pair.second->attributes<Place2dNodeAttributes>();
    if (attrs.boundary.size() <= 1) {
      continue;
    }

    color = makeColorMsg(attrs.color, config.boundary_ellipse_alpha);
    const auto pos = attrs.position;
    last_point.x = attrs.ellipse_matrix_expand(0, 0) + attrs.ellipse_centroid(0);
    last_point.y = attrs.ellipse_matrix_expand(1, 0) + attrs.ellipse_centroid(1);
    last_point.z = pos.z();

    int npts = 20;
    for (int ix = 1; ix < npts + 1; ++ix) {
      marker.points.push_back(last_point);
      marker.colors.push_back(color);

      float t = ix * 2 * M_PI / npts;
      Eigen::Vector2d p2 =
          attrs.ellipse_matrix_expand * Eigen::Vector2d(cos(t), sin(t));
      last_point.x = p2(0) + attrs.ellipse_centroid(0);
      last_point.y = p2(1) + attrs.ellipse_centroid(1);
      last_point.z = pos.z();

      marker.points.push_back(last_point);
      marker.colors.push_back(color);
    }
  }
  return marker;
}

Marker makeLayerPolygonEdges(const std_msgs::Header& header,
                             const LayerConfig& config,
                             const SceneGraphLayer& layer,
                             const VisualizerConfig& visualizer_config,
                             const std::string& ns) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.id = 0;
  marker.ns = ns;
  marker.scale.x = config.boundary_wireframe_scale;
  fillPoseWithIdentity(marker.pose);

  for (const auto& id_node_pair : layer.nodes()) {
    const auto& attrs = id_node_pair.second->attributes<Place2dNodeAttributes>();
    if (attrs.boundary.size() <= 1) {
      continue;
    }

    const auto pos = attrs.position;
    geometry_msgs::Point node_point;
    tf2::convert(pos, node_point);
    node_point.z += getZOffset(config, visualizer_config);
    const auto color = makeColorMsg(attrs.color, config.boundary_alpha);

    for (size_t i = 0; i < attrs.boundary.size(); ++i) {
      geometry_msgs::Point boundary_point;
      tf2::convert(attrs.boundary[i], boundary_point);
      boundary_point.z = pos.z();

      marker.points.push_back(boundary_point);
      marker.colors.push_back(color);
      marker.points.push_back(node_point);
      marker.colors.push_back(color);
    }
  }

  return marker;
}

Marker makeLayerPolygonBoundaries(const std_msgs::Header& header,
                                  const LayerConfig& config,
                                  const SceneGraphLayer& layer,
                                  const VisualizerConfig& visualizer_config,
                                  const std::string& ns) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.id = 0;
  marker.ns = ns;
  marker.scale.x = config.boundary_wireframe_scale;

  fillPoseWithIdentity(marker.pose);
  marker.pose.position.z +=
      config.collapse_boundary ? 0.0 : getZOffset(config, visualizer_config);

  for (const auto& id_node_pair : layer.nodes()) {
    const auto& attrs = id_node_pair.second->attributes<Place2dNodeAttributes>();
    if (attrs.boundary.size() <= 1) {
      continue;
    }

    const auto pos = attrs.position;

    std_msgs::ColorRGBA color;
    if (config.boundary_use_node_color) {
      color = makeColorMsg(attrs.color, config.boundary_alpha);
    } else {
      color = makeColorMsg(Color(), config.boundary_alpha);
    }

    geometry_msgs::Point last_point;
    tf2::convert(attrs.boundary.back(), last_point);
    last_point.z = pos.z();

    for (size_t i = 0; i < attrs.boundary.size(); ++i) {
      marker.points.push_back(last_point);
      marker.colors.push_back(color);

      tf2::convert(attrs.boundary[i], last_point);
      last_point.z = pos.z();
      marker.points.push_back(last_point);
      marker.colors.push_back(color);
    }
  }

  return marker;
}

geometry_msgs::Point getPointFromMatrix(const Eigen::MatrixXf& matrix, int col) {
  geometry_msgs::Point point;
  point.x = matrix(0, col);
  point.y = matrix(1, col);
  point.z = matrix(2, col);
  return point;
}

void fillCornersFromBbox(const BoundingBox& bbox, Eigen::MatrixXf& corners) {
  const static std::array<size_t, 8> remapping{0, 1, 3, 2, 4, 5, 7, 6};
  const auto corner_array = bbox.corners();
  for (int i = 0; i < 8; ++i) {
    corners.block<3, 1>(0, i) = corner_array[remapping[i]];
  }
}

void addWireframeToMarker(const Eigen::MatrixXf& corners,
                          const std_msgs::ColorRGBA& color,
                          Marker& marker) {
  for (int c = 0; c < corners.cols(); ++c) {
    // edges are 1-bit pertubations
    int x_neighbor = c | 0x01;
    int y_neighbor = c | 0x02;
    int z_neighbor = c | 0x04;
    if (c != x_neighbor) {
      marker.points.push_back(getPointFromMatrix(corners, c));
      marker.colors.push_back(color);
      marker.points.push_back(getPointFromMatrix(corners, x_neighbor));
      marker.colors.push_back(color);
    }
    if (c != y_neighbor) {
      marker.points.push_back(getPointFromMatrix(corners, c));
      marker.colors.push_back(color);
      marker.points.push_back(getPointFromMatrix(corners, y_neighbor));
      marker.colors.push_back(color);
    }
    if (c != z_neighbor) {
      marker.points.push_back(getPointFromMatrix(corners, c));
      marker.colors.push_back(color);
      marker.points.push_back(getPointFromMatrix(corners, z_neighbor));
      marker.colors.push_back(color);
    }
  }
}

void addEdgesToCorners(const Eigen::MatrixXf& corners,
                       const geometry_msgs::Point& node_centroid,
                       const std_msgs::ColorRGBA& color,
                       Marker& marker) {
  for (size_t i = 0; i < 8; ++i) {
    marker.colors.push_back(color);
  }

  // top box corners are 4, 5, 6, 7
  marker.points.push_back(node_centroid);
  marker.points.push_back(getPointFromMatrix(corners, 4));
  marker.points.push_back(node_centroid);
  marker.points.push_back(getPointFromMatrix(corners, 5));
  marker.points.push_back(node_centroid);
  marker.points.push_back(getPointFromMatrix(corners, 6));
  marker.points.push_back(node_centroid);
  marker.points.push_back(getPointFromMatrix(corners, 7));
}

Marker makeEdgesToBoundingBoxes(const std_msgs::Header& header,
                                const LayerConfig& config,
                                const SceneGraphLayer& layer,
                                const VisualizerConfig& visualizer_config,
                                const std::string& ns,
                                const ColorFunction& func,
                                const FilterFunction& filter) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.id = 0;
  marker.ns = ns;
  marker.scale.x = config.bbox_wireframe_edge_scale;

  fillPoseWithIdentity(marker.pose);

  marker.points.reserve(8 * layer.numNodes());
  marker.colors.reserve(8 * layer.numNodes());

  Eigen::MatrixXf corners(3, 8);
  for (const auto& id_node_pair : layer.nodes()) {
    if (filter && !filter(*id_node_pair.second)) {
      continue;
    }

    const auto& attrs = id_node_pair.second->attributes<SemanticNodeAttributes>();
    const auto color =
        makeColorMsg(func(*id_node_pair.second), config.bounding_box_alpha);
    fillCornersFromBbox(attrs.bounding_box, corners);

    geometry_msgs::Point node_centroid;
    tf2::convert(attrs.position, node_centroid);
    node_centroid.z += getZOffset(config, visualizer_config);

    geometry_msgs::Point center_point;
    tf2::convert(attrs.position, center_point);
    center_point.z +=
        visualizer_config.mesh_edge_break_ratio * getZOffset(config, visualizer_config);

    marker.points.push_back(node_centroid);
    marker.colors.push_back(color);
    marker.points.push_back(center_point);
    marker.colors.push_back(color);

    addEdgesToCorners(corners, center_point, color, marker);
  }

  return marker;
}

Marker makeLayerWireframeBoundingBoxes(const std_msgs::Header& header,
                                       const LayerConfig& config,
                                       const SceneGraphLayer& layer,
                                       const VisualizerConfig& visualizer_config,
                                       const std::string& ns,
                                       const ColorFunction& func,
                                       const FilterFunction& filter) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.id = 0;
  marker.ns = ns;
  marker.scale.x = config.bbox_wireframe_scale;

  fillPoseWithIdentity(marker.pose);
  marker.pose.position.z +=
      config.collapse_bounding_box ? 0.0 : getZOffset(config, visualizer_config);

  marker.points.reserve(12 * layer.numNodes());
  marker.colors.reserve(12 * layer.numNodes());

  Eigen::MatrixXf corners(3, 8);
  for (const auto& id_node_pair : layer.nodes()) {
    if (filter && !filter(*id_node_pair.second)) {
      continue;
    }

    const auto& attrs = id_node_pair.second->attributes<SemanticNodeAttributes>();
    const auto color =
        makeColorMsg(func(*id_node_pair.second), config.bounding_box_alpha);
    fillCornersFromBbox(attrs.bounding_box, corners);
    addWireframeToMarker(corners, color, marker);
  }

  return marker;
}

Marker makeBoundingBoxMarker(const std_msgs::Header& header,
                             const LayerConfig& config,
                             const SceneGraphNode& node,
                             const VisualizerConfig& visualizer_config,
                             const std::string& ns,
                             const ColorFunction& func) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::CUBE;
  marker.action = Marker::ADD;
  marker.id = node.id;
  marker.ns = ns;
  marker.color = makeColorMsg(func(node), config.bounding_box_alpha);

  const BoundingBox& bounding_box =
      node.attributes<SemanticNodeAttributes>().bounding_box;

  Eigen::Quaternionf world_q_center = Eigen::Quaternionf(bounding_box.world_R_center);
  marker.pose.position = tf2::toMsg(bounding_box.world_P_center.cast<double>().eval());
  tf2::convert(world_q_center.cast<double>(), marker.pose.orientation);
  marker.pose.position.z +=
      config.collapse_bounding_box ? 0.0 : getZOffset(config, visualizer_config);
  tf2::toMsg(bounding_box.dimensions.cast<double>().eval(), marker.scale);

  return marker;
}

Marker makeTextMarker(const std_msgs::Header& header,
                      const LayerConfig& config,
                      const SceneGraphNode& node,
                      const VisualizerConfig& visualizer_config,
                      const std::string& ns) {
  Marker marker;
  marker.header = header;
  marker.ns = ns;
  marker.id = node.id;
  marker.type = Marker::TEXT_VIEW_FACING;
  marker.action = Marker::ADD;
  marker.lifetime = ros::Duration(0);
  std::string name;
  try {
    name = node.attributes<SemanticNodeAttributes>().name;
  } catch (const std::exception&) {
  }
  marker.text = name.empty() ? NodeSymbol(node.id).getLabel() : name;
  marker.scale.z = config.label_scale;
  marker.color = makeColorMsg(Color());

  fillPoseWithIdentity(marker.pose);
  tf2::convert(node.attributes().position, marker.pose.position);
  marker.pose.position.z += getZOffset(config, visualizer_config) + config.label_height;

  if (config.add_label_jitter) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution dist(-1.0, 1.0);
    const auto z_jitter = config.label_jitter_scale * dist(gen);
    marker.pose.position.z += z_jitter;
  }

  return marker;
}

Marker makeTextMarkerNoHeight(const std_msgs::Header& header,
                              const LayerConfig& config,
                              const SceneGraphNode& node,
                              const VisualizerConfig&,
                              const std::string& ns,
                              const ros::NodeHandle& nh) {
  Marker marker;
  marker.header = header;
  marker.ns = ns;
  marker.id = node.id;
  marker.type = Marker::TEXT_VIEW_FACING;
  marker.action = Marker::ADD;
  marker.lifetime = ros::Duration(0);

  // ! Changes
  int semantic_label = node.attributes<SemanticNodeAttributes>().semantic_label;
  std::string unique_id = node.attributes<SemanticNodeAttributes>().name;

  // Use regex to extract the number inside "O(x)"
  std::regex number_regex(R"(O\((\d+)\))");
  std::smatch match;

  if (std::regex_search(unique_id, match, number_regex) && match.size() > 1) {
      unique_id = match[1].str();  // Extract only the number
  } else {
      ROS_WARN("Could not extract number from name: %s", unique_id.c_str());
  }

  // Retrieve label_names from ROS parameters
  XmlRpc::XmlRpcValue label_names;
  if (!nh.getParam("/label_names", label_names)) {
      ROS_WARN("Could not retrieve label_names from ROS parameters.");
  }

  // Ensure label_names is an array
  std::string label_text = "Unknown";
  if (label_names.getType() == XmlRpc::XmlRpcValue::TypeArray) {
      for (int i = 0; i < label_names.size(); ++i) {
          if (label_names[i].getType() == XmlRpc::XmlRpcValue::TypeStruct &&
              label_names[i].hasMember("label") &&
              label_names[i].hasMember("name")) {

              int label_id = static_cast<int>(label_names[i]["label"]);
              std::string name = static_cast<std::string>(label_names[i]["name"]);

              if (label_id == semantic_label) {
                  label_text = name;  // Store the label name
                  break;
              }
          }
      }
  } else {
      ROS_WARN("label_names is not formatted as an array in ROS parameters.");
  }

  // Construct the marker text
  std::stringstream ss;
  ss << label_text << "(" << unique_id << ")";

  marker.text = ss.str();

  marker.scale.z = config.label_scale;
  marker.color = makeColorMsg(Color());

  std::string param_key = "/semantic_labels/" + unique_id;
  nh.setParam(param_key, label_text);

  fillPoseWithIdentity(marker.pose);
  tf2::convert(node.attributes().position, marker.pose.position);
  marker.pose.position.z += config.label_height;

  return marker;
}

std::vector<Marker> makeEllipsoidMarkers(const std_msgs::Header& header,
                                         const LayerConfig& config,
                                         const SceneGraphLayer& layer,
                                         const VisualizerConfig& visualizer_config,
                                         const std::string& ns,
                                         const ColorFunction& color_func) {
  size_t id = 0;
  std::vector<Marker> markers;
  for (const auto& id_node_pair : layer.nodes()) {
    const auto& attrs = id_node_pair.second->attributes<PlaceNodeAttributes>();
    if (attrs.real_place) {
      continue;
    }
    Marker marker;
    marker.header = header;
    marker.type = Marker::SPHERE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.id = id++;
    marker.ns = ns;

    marker.scale.x = attrs.frontier_scale.x();
    marker.scale.y = attrs.frontier_scale.y();
    marker.scale.z = attrs.frontier_scale.z();

    tf2::convert(attrs.position, marker.pose.position);
    tf2::convert(attrs.orientation, marker.pose.orientation);

    marker.pose.position.z += getZOffset(config, visualizer_config);
    marker.color = makeColorMsg(color_func(*id_node_pair.second), config.marker_alpha);

    Color desired_color = color_func(*id_node_pair.second);
    marker.colors.push_back(makeColorMsg(desired_color, config.marker_alpha));
    markers.push_back(marker);
  }
  return markers;
}

Marker makeCentroidMarkers(const std_msgs::Header& header,
                           const LayerConfig& config,
                           const SceneGraphLayer& layer,
                           const VisualizerConfig& visualizer_config,
                           const std::string& ns,
                           const ColorFunction& color_func,
                           const FilterFunction& filter) {
  Marker marker;
  marker.header = header;
  marker.type = config.use_sphere_marker ? Marker::SPHERE_LIST : Marker::CUBE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.id = 0;
  marker.ns = ns;

  marker.scale.x = config.marker_scale;
  marker.scale.y = config.marker_scale;
  marker.scale.z = config.marker_scale;

  fillPoseWithIdentity(marker.pose);

  marker.points.reserve(layer.numNodes());
  marker.colors.reserve(layer.numNodes());
  for (const auto& id_node_pair : layer.nodes()) {
    if (filter && !filter(*id_node_pair.second)) {
      continue;
    }

    geometry_msgs::Point node_centroid;
    tf2::convert(id_node_pair.second->attributes().position, node_centroid);
    node_centroid.z += getZOffset(config, visualizer_config);
    marker.points.push_back(node_centroid);

    Color desired_color = color_func(*id_node_pair.second);
    marker.colors.push_back(makeColorMsg(desired_color, config.marker_alpha));
  }

  return marker;
}

Marker makePlaceCentroidMarkers(const std_msgs::Header& header,
                                const LayerConfig& config,
                                const SceneGraphLayer& layer,
                                const VisualizerConfig& visualizer_config,
                                const std::string& ns,
                                const ColorFunction& color_func) {
  Marker marker;
  marker.header = header;
  marker.type = config.use_sphere_marker ? Marker::SPHERE_LIST : Marker::CUBE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.id = 0;
  marker.ns = ns;

  marker.scale.x = config.marker_scale;
  marker.scale.y = config.marker_scale;
  marker.scale.z = config.marker_scale;

  fillPoseWithIdentity(marker.pose);

  marker.points.reserve(layer.numNodes());
  marker.colors.reserve(layer.numNodes());
  for (const auto& id_node_pair : layer.nodes()) {
    const auto& attrs = id_node_pair.second->attributes<PlaceNodeAttributes>();
    if (!attrs.real_place) {
      continue;
    }
    geometry_msgs::Point node_centroid;
    tf2::convert(attrs.position, node_centroid);
    node_centroid.z += getZOffset(config, visualizer_config);
    marker.points.push_back(node_centroid);

    Color desired_color = color_func(*id_node_pair.second);
    marker.colors.push_back(makeColorMsg(desired_color, config.marker_alpha));
  }

  return marker;
}

namespace {

inline Marker makeNewEdgeList(const std_msgs::Header& header,
                              const LayerConfig& config,
                              const std::string& ns_prefix,
                              LayerId source,
                              LayerId target) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.action = Marker::ADD;
  marker.id = 0;
  marker.ns = ns_prefix + std::to_string(source) + "_" + std::to_string(target);
  marker.scale.x = config.interlayer_edge_scale;
  fillPoseWithIdentity(marker.pose);
  return marker;
}

}  // namespace

bool shouldVisualize(const DynamicSceneGraph& graph,
                     const SceneGraphNode& node,
                     const std::map<LayerId, LayerConfig>& configs,
                     const std::map<LayerId, DynamicLayerConfig>& dynamic_configs) {
  if (graph.isDynamic(node.id)) {
    return dynamic_configs.count(node.layer) &&
           dynamic_configs.at(node.layer).visualize &&
           dynamic_configs.at(node.layer).visualize_interlayer_edges;
  }

  return configs.count(node.layer) && configs.at(node.layer).visualize;
}

LayerId getConfigLayer(const DynamicSceneGraph& graph,
                       const SceneGraphNode& source,
                       const SceneGraphNode& target) {
  if (graph.isDynamic(source.id)) {
    return source.layer;
  } else {
    return target.layer;
  }
}

MarkerArray makeDynamicGraphEdgeMarkers(
    const std_msgs::Header& header,
    const DynamicSceneGraph& graph,
    const std::map<LayerId, LayerConfig>& configs,
    const std::map<LayerId, DynamicLayerConfig>& dynamic_configs,
    const VisualizerConfig& visualizer_config,
    const std::string& ns_prefix) {
  MarkerArray layer_edges;
  std::map<LayerId, Marker> layer_markers;
  std::map<LayerId, size_t> num_since_last_insertion;

  for (const auto& edge : graph.dynamic_interlayer_edges()) {
    const auto& source = graph.getNode(edge.second.source);
    const auto& target = graph.getNode(edge.second.target);

    if (!shouldVisualize(graph, source, configs, dynamic_configs)) {
      continue;
    }

    if (!shouldVisualize(graph, target, configs, dynamic_configs)) {
      continue;
    }

    DynamicLayerConfig config =
        dynamic_configs.at(getConfigLayer(graph, source, target));

    size_t num_between_insertions = config.interlayer_edge_insertion_skip;

    if (layer_markers.count(source.layer) == 0) {
      layer_markers[source.layer] = makeNewEdgeList(
          header, configs.at(source.layer), ns_prefix, source.layer, target.layer);
      layer_markers[source.layer].color =
          makeColorMsg(Color(), config.edge_alpha);
      // make sure we always draw at least one edge
      num_since_last_insertion[source.layer] = num_between_insertions;
    }

    if (num_since_last_insertion[source.layer] >= num_between_insertions) {
      num_since_last_insertion[source.layer] = 0;
    } else {
      num_since_last_insertion[source.layer]++;
      continue;
    }

    Marker& marker = layer_markers.at(source.layer);
    geometry_msgs::Point source_point;
    tf2::convert(source.attributes().position, source_point);
    source_point.z += getZOffset(configs.at(source.layer), visualizer_config);
    marker.points.push_back(source_point);

    geometry_msgs::Point target_point;
    tf2::convert(target.attributes().position, target_point);
    target_point.z += getZOffset(configs.at(target.layer), visualizer_config);
    marker.points.push_back(target_point);
  }

  for (const auto& id_marker_pair : layer_markers) {
    layer_edges.markers.push_back(id_marker_pair.second);
  }
  return layer_edges;
}

// TODO(nathan) consider making this shorter
MarkerArray makeGraphEdgeMarkers(const std_msgs::Header& header,
                                 const DynamicSceneGraph& graph,
                                 const std::map<LayerId, LayerConfig>& configs,
                                 const VisualizerConfig& visualizer_config,
                                 const std::string& ns_prefix,
                                 const FilterFunction& filter) {
  MarkerArray layer_edges;
  std::map<LayerId, Marker> layer_markers;
  std::map<LayerId, size_t> num_since_last_insertion;

  for (const auto& edge : graph.interlayer_edges()) {
    const auto& source = graph.getNode(edge.second.source);
    const auto& target = graph.getNode(edge.second.target);

    if (filter && !filter(source)) {
      continue;
    }

    if (filter && !filter(target)) {
      continue;
    }

    if (!configs.count(source.layer) || !configs.count(target.layer)) {
      continue;
    }

    if (!configs.at(source.layer).visualize) {
      continue;
    }

    if (!configs.at(target.layer).visualize) {
      continue;
    }

    size_t num_between_insertions =
        configs.at(source.layer).interlayer_edge_insertion_skip;

    // parent is always source
    // TODO(nathan) make the above statement an invariant
    if (layer_markers.count(source.layer) == 0) {
      layer_markers[source.layer] = makeNewEdgeList(
          header, configs.at(source.layer), ns_prefix, source.layer, target.layer);
      // make sure we always draw at least one edge
      num_since_last_insertion[source.layer] = num_between_insertions;
    }

    if (num_since_last_insertion[source.layer] >= num_between_insertions) {
      num_since_last_insertion[source.layer] = 0;
    } else {
      num_since_last_insertion[source.layer]++;
      continue;
    }

    Marker& marker = layer_markers.at(source.layer);
    geometry_msgs::Point source_point;
    tf2::convert(source.attributes().position, source_point);
    source_point.z += getZOffset(configs.at(source.layer), visualizer_config);
    marker.points.push_back(source_point);

    geometry_msgs::Point target_point;
    tf2::convert(target.attributes().position, target_point);
    target_point.z += getZOffset(configs.at(target.layer), visualizer_config);
    marker.points.push_back(target_point);

    Color edge_color;
    if (configs.at(source.layer).interlayer_edge_use_color) {
      if (configs.at(source.layer).use_edge_source) {
        // TODO(nathan) this might not be a safe cast in general
        edge_color = source.attributes<SemanticNodeAttributes>().color;
      } else {
        // TODO(nathan) this might not be a safe cast in general
        edge_color = target.attributes<SemanticNodeAttributes>().color;
      }
    } else {
      edge_color = Color();
    }

    marker.colors.push_back(
        makeColorMsg(edge_color, configs.at(source.layer).intralayer_edge_alpha));
    marker.colors.push_back(
        makeColorMsg(edge_color, configs.at(source.layer).intralayer_edge_alpha));
  }

  for (const auto& id_marker_pair : layer_markers) {
    layer_edges.markers.push_back(id_marker_pair.second);
  }
  return layer_edges;
}

Marker makeMeshEdgesMarker(const std_msgs::Header& header,
                           const LayerConfig& config,
                           const VisualizerConfig& visualizer_config,
                           const DynamicSceneGraph& graph,
                           const SceneGraphLayer& layer,
                           const std::string& ns) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.action = Marker::ADD;
  marker.id = 0;
  marker.ns = ns;

  marker.scale.x = config.interlayer_edge_scale;
  fillPoseWithIdentity(marker.pose);

  const auto mesh = graph.mesh();
  if (!mesh) {
    return marker;
  }

  for (const auto& id_node_pair : layer.nodes()) {
    const auto& node = *id_node_pair.second;
    const auto& attrs = node.attributes<Place2dNodeAttributes>();
    const auto& mesh_edge_indices = attrs.pcl_mesh_connections;
    if (mesh_edge_indices.empty()) {
      continue;
    }

    geometry_msgs::Point center_point;
    tf2::convert(attrs.position, center_point);
    center_point.z +=
        visualizer_config.mesh_edge_break_ratio * getZOffset(config, visualizer_config);

    geometry_msgs::Point centroid_location;
    tf2::convert(attrs.position, centroid_location);
    centroid_location.z += getZOffset(config, visualizer_config);

    // make first edge
    marker.points.push_back(centroid_location);
    marker.points.push_back(center_point);
    if (config.interlayer_edge_use_color) {
      marker.colors.push_back(makeColorMsg(attrs.color, config.interlayer_edge_alpha));
      marker.colors.push_back(makeColorMsg(attrs.color, config.interlayer_edge_alpha));
    } else {
      marker.colors.push_back(
          makeColorMsg(Color(), config.interlayer_edge_alpha));
      marker.colors.push_back(
          makeColorMsg(Color(), config.interlayer_edge_alpha));
    }

    size_t i = 0;
    for (const auto midx : mesh_edge_indices) {
      ++i;
      if ((i - 1) % (config.interlayer_edge_insertion_skip + 1) != 0) {
        continue;
      }

      if (midx >= mesh->numVertices()) {
        continue;
      }

      Eigen::Vector3d vertex_pos = mesh->pos(midx).cast<double>();
      geometry_msgs::Point vertex;
      tf2::convert(vertex_pos, vertex);
      if (!visualizer_config.collapse_layers) {
        vertex.z += visualizer_config.mesh_layer_offset;
      }

      marker.points.push_back(center_point);
      marker.points.push_back(vertex);

      if (config.interlayer_edge_use_color) {
        marker.colors.push_back(
            makeColorMsg(attrs.color, config.interlayer_edge_alpha));
        marker.colors.push_back(
            makeColorMsg(attrs.color, config.interlayer_edge_alpha));
      } else {
        marker.colors.push_back(
            makeColorMsg(Color(), config.interlayer_edge_alpha));
        marker.colors.push_back(
            makeColorMsg(Color(), config.interlayer_edge_alpha));
      }
    }
  }

  return marker;
}

MarkerArray makeGvdWireframe(const std_msgs::Header& header,
                             const LayerConfig& config,
                             const VisualizerConfig& visualizer_config,
                             const SceneGraphLayer& layer,
                             const std::string& ns,
                             const ColormapConfig& colors,
                             size_t marker_id) {
  return makeGvdWireframe(
      header,
      config,
      layer,
      ns,
      [&](const SceneGraphNode& node) {
        return getDistanceColor(
            visualizer_config, colors, node.attributes<PlaceNodeAttributes>().distance);
      },
      marker_id);
}

MarkerArray makeGvdWireframe(const std_msgs::Header& header,
                             const LayerConfig& config,
                             const SceneGraphLayer& layer,
                             const std::string& ns,
                             const ColorFunction& color_func,
                             size_t marker_id) {
  MarkerArray marker;
  {  // scope to make handling stuff a little easier
    Marker edges;
    edges.header = header;
    edges.type = Marker::LINE_LIST;
    edges.id = marker_id;
    edges.ns = ns + "_edges";
    edges.action = Marker::ADD;
    edges.scale.x = config.intralayer_edge_scale;
    fillPoseWithIdentity(edges.pose);

    Marker nodes;
    nodes.header = header;
    nodes.type = Marker::SPHERE_LIST;
    nodes.id = marker_id;
    nodes.ns = ns + "_nodes";
    nodes.action = Marker::ADD;
    nodes.scale.x = config.intralayer_edge_scale;
    nodes.scale.y = config.intralayer_edge_scale;
    nodes.scale.z = config.intralayer_edge_scale;
    fillPoseWithIdentity(nodes.pose);

    marker.markers.push_back(nodes);
    marker.markers.push_back(edges);
  }
  auto& nodes = marker.markers[0];
  auto& edges = marker.markers[1];

  if (layer.nodes().empty()) {
    marker.markers.clear();
    return marker;
  }

  for (const auto& id_node_pair : layer.nodes()) {
    geometry_msgs::Point node_centroid;
    tf2::convert(id_node_pair.second->attributes().position, node_centroid);
    nodes.points.push_back(node_centroid);

    Color desired_color = color_func(*id_node_pair.second);
    nodes.colors.push_back(makeColorMsg(desired_color, config.marker_alpha));
  }

  if (layer.edges().empty()) {
    marker.markers.resize(1);
    return marker;
  }

  for (const auto& id_edge_pair : layer.edges()) {
    // TODO(nathan) filter by node symbol category
    const auto& edge = id_edge_pair.second;
    const auto& source_node = layer.getNode(edge.source);
    const auto& target_node = layer.getNode(edge.target);

    geometry_msgs::Point source;
    tf2::convert(source_node.attributes().position, source);
    edges.points.push_back(source);

    geometry_msgs::Point target;
    tf2::convert(target_node.attributes().position, target);
    edges.points.push_back(target);

    edges.colors.push_back(makeColorMsg(color_func(source_node), config.marker_alpha));
    edges.colors.push_back(makeColorMsg(color_func(target_node), config.marker_alpha));
  }

  return marker;
}

Marker makeLayerEdgeMarkers(const std_msgs::Header& header,
                            const LayerConfig& config,
                            const SceneGraphLayer& layer,
                            const VisualizerConfig& visualizer_config,
                            const Color& color,
                            const std::string& ns,
                            const FilterFunction& filter) {
  return makeLayerEdgeMarkers(
      header,
      config,
      layer,
      visualizer_config,
      ns,
      [&](const SceneGraphNode&, const SceneGraphNode&, const SceneGraphEdge&, bool) {
        return color;
      },
      filter);
}

Marker makeLayerEdgeMarkers(const std_msgs::Header& header,
                            const LayerConfig& config,
                            const SceneGraphLayer& layer,
                            const VisualizerConfig& visualizer_config,
                            const ColormapConfig& cmap,
                            const std::string& ns,
                            const FilterFunction& filter) {
  return makeLayerEdgeMarkers(
      header,
      config,
      layer,
      visualizer_config,
      ns,
      [&](const SceneGraphNode&,
          const SceneGraphNode&,
          const SceneGraphEdge& edge,
          bool) {
        return getDistanceColor(visualizer_config, cmap, edge.attributes().weight);
      },
      filter);
}

Marker makeLayerEdgeMarkers(const std_msgs::Header& header,
                            const LayerConfig& config,
                            const SceneGraphLayer& layer,
                            const VisualizerConfig& visualizer_config,
                            const std::string& ns,
                            const EdgeColorFunction& color_func,
                            const FilterFunction& filter) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.id = 0;
  marker.ns = ns;

  marker.action = Marker::ADD;
  marker.scale.x = config.intralayer_edge_scale;
  fillPoseWithIdentity(marker.pose);

  auto edge_iter = layer.edges().begin();
  while (edge_iter != layer.edges().end()) {
    const auto& source_node = layer.getNode(edge_iter->second.source);
    const auto& target_node = layer.getNode(edge_iter->second.target);
    if (filter && (!filter(source_node) || !filter(target_node))) {
      continue;
    }

    geometry_msgs::Point source;
    tf2::convert(source_node.attributes().position, source);
    source.z += getZOffset(config, visualizer_config);
    marker.points.push_back(source);

    geometry_msgs::Point target;
    tf2::convert(target_node.attributes().position, target);
    target.z += getZOffset(config, visualizer_config);
    marker.points.push_back(target);

    marker.colors.push_back(
        makeColorMsg(color_func(source_node, target_node, edge_iter->second, true),
                     config.intralayer_edge_alpha));
    marker.colors.push_back(
        makeColorMsg(color_func(source_node, target_node, edge_iter->second, false),
                     config.intralayer_edge_alpha));

    std::advance(edge_iter, config.intralayer_edge_insertion_skip + 1);
  }

  return marker;
}

Marker makeDynamicCentroidMarkers(const std_msgs::Header& header,
                                  const DynamicLayerConfig& config,
                                  const DynamicSceneGraphLayer& layer,
                                  const VisualizerConfig& visualizer_config,
                                  const Color& color,
                                  const std::string& ns,
                                  size_t marker_id) {
  return makeDynamicCentroidMarkers(
      header,
      config,
      layer,
      config.z_offset_scale,
      visualizer_config,
      ns,
      [&](const auto&) -> Color { return color; },
      marker_id);
}

Marker makeDynamicCentroidMarkers(const std_msgs::Header& header,
                                  const DynamicLayerConfig& config,
                                  const DynamicSceneGraphLayer& layer,
                                  double layer_offset_scale,
                                  const VisualizerConfig& visualizer_config,
                                  const std::string& ns,
                                  const ColorFunction& color_func,
                                  size_t marker_id) {
  Marker marker;
  marker.header = header;
  marker.type = config.node_use_sphere ? Marker::SPHERE_LIST : Marker::CUBE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.ns = ns;
  marker.id = marker_id;

  marker.scale.x = config.node_scale;
  marker.scale.y = config.node_scale;
  marker.scale.z = config.node_scale;

  fillPoseWithIdentity(marker.pose);

  marker.points.reserve(layer.numNodes());
  for (const auto& node : layer.nodes()) {
    if (!node) {
      continue;
    }

    geometry_msgs::Point node_centroid;
    tf2::convert(node->attributes().position, node_centroid);
    node_centroid.z += getZOffset(layer_offset_scale, visualizer_config);
    marker.points.push_back(node_centroid);
    marker.colors.push_back(makeColorMsg(color_func(*node), config.node_alpha));
  }

  return marker;
}

Marker makeDynamicEdgeMarkers(const std_msgs::Header& header,
                              const DynamicLayerConfig& config,
                              const DynamicSceneGraphLayer& layer,
                              const VisualizerConfig& visualizer_config,
                              const Color& color,
                              const std::string& ns,
                              size_t marker_id) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::LINE_LIST;
  marker.ns = ns;
  marker.id = marker_id;

  marker.action = Marker::ADD;
  marker.scale.x = config.edge_scale;
  marker.color = makeColorMsg(color, config.edge_alpha);
  fillPoseWithIdentity(marker.pose);

  for (const auto& id_edge_pair : layer.edges()) {
    geometry_msgs::Point source;
    tf2::convert(layer.getPosition(id_edge_pair.second.source), source);
    source.z += getZOffset(config.z_offset_scale, visualizer_config);
    marker.points.push_back(source);

    geometry_msgs::Point target;
    tf2::convert(layer.getPosition(id_edge_pair.second.target), target);
    target.z += getZOffset(config.z_offset_scale, visualizer_config);
    marker.points.push_back(target);
  }

  return marker;
}

Marker makeDynamicLabelMarker(const std_msgs::Header& header,
                              const DynamicLayerConfig& config,
                              const DynamicSceneGraphLayer& layer,
                              const VisualizerConfig& visualizer_config,
                              const std::string& ns,
                              size_t marker_id) {
  Marker marker;
  marker.header = header;
  marker.type = Marker::TEXT_VIEW_FACING;
  marker.ns = ns;
  marker.id = marker_id;
  marker.action = Marker::ADD;
  marker.lifetime = ros::Duration(0);
  marker.text = "Agent";  // std::to_string(layer.id) + ":" + layer.prefix.str();
  marker.scale.z = config.label_scale;
  marker.color = makeColorMsg(Color());

  Eigen::Vector3d latest_position = layer.getPositionByIndex(layer.numNodes() - 1);
  fillPoseWithIdentity(marker.pose);
  tf2::convert(latest_position, marker.pose.position);
  marker.pose.position.z +=
      getZOffset(config.z_offset_scale, visualizer_config) + config.label_height;

  return marker;
}

}  // namespace hydra
