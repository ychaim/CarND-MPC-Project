#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}


// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

Eigen::VectorXd toVectorXd(vector<double> v){
  Eigen::VectorXd vxd(v.size());
  for(unsigned int i = 0; i < vxd.size(); ++i){
    vxd(i) = v[i];
  }
  return vxd;
}


void to_vehicle_coords(vector<double> &xs, vector<double> &ys, double px, double py, double theta){
    // First step is to convert to vehicle coordinates
    for(unsigned int i = 0; i < xs.size(); ++i){
      // First translate the coordinates to be in the vehicle's reference frame
      double x = xs[i] - px;
      double y = ys[i] - py;

      // Now rotate the angle clockiwse by psi.
      // a positive psi implies a right turn
      // while a negative one implies a left turn
      // if we rotate counterclockwise then we negate psi
      xs[i] = x * cos(-theta) - sin(-theta) * y;
      ys[i] = x * sin(-theta) + cos(-theta) * y;    
      
      // TODO use clockwise rotation for this exercise
      // double clockwise_x = x * cos(psi) + sin(psi) * y;
      // double clockwise_y = -(x * sin(psi)) + cos(psi) * y;


      // cout << "clockwise x[" << i << "]=" << clockwise_x
      //      << " - counter-clockise=" << ptsx[i] << endl;

      // cout << "clockwise y[" << i << "]=" << clockwise_y
      //      << " - counter-clockise=" << ptsy[i] << endl;        
    }
}

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    cout << sdata << endl;
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
      string s = hasData(sdata);
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          vector<double> ptsx = j[1]["ptsx"];
          vector<double> ptsy = j[1]["ptsy"];
          double px = j[1]["x"];
          double py = j[1]["y"];
          double psi = j[1]["psi"];
          double v = j[1]["speed"];

          to_vehicle_coords(ptsx, ptsy, px, py, psi);
          // Now px and py become 0 since they are the center of the system
          px = 0.0;
          py = 0.0;   
          // Same for psi as we have rotated our coordinate system by psi
          psi = 0.0;       
                                                 
          // First step is to compute the polynomial coefficients given ptsx and ptsy
          Eigen::VectorXd vx = toVectorXd(ptsx);
          Eigen::VectorXd vy = toVectorXd(ptsy);    

          auto coeffs = polyfit(vx, vy, 3);          
                            
          // Get the predicted y based on the polynomial we calculated above
          // double fx = polyeval(coeffs, px);
          double fx = coeffs[0] + coeffs[1] * px + coeffs[2] * (px * px) + coeffs[3] * (px * px * px);
          // CTE is just the difference between our predicted y and the vehicle's actual y
          double cte = fx - py;

          // Compute the derivative at point x 
          // double fprime_x = poly_der(coeffs, px);
          double fprime_x = coeffs[1] + 2 * coeffs[2] * px + 3 * coeffs[3] * (px * px);
          // And use it to calculate the desired angle psi
          double desired_psi = -atan(fprime_x);
          // Now the error for psi is the difference between the current psi and our derired psi
          double epsi = psi - desired_psi;

          // We can now create our state vector
          Eigen::VectorXd state(6);

          // Here our angle psi is naturally 0 as we have moved the waypoints
          // to the car's coordinate system and orientation
          // Also since we moved to car coordinate system, x and y are 0
          state << px, py, psi, v, cte, epsi;

          cout << "State is " << state[0] << ","
                              << state[1] << ","
                              << state[2] << ","
                              << state[3] << ","
                              << state[4] << ","
                              << state[5]
                              << endl;          

          /*
          * TODO: Calculate steering angle and throttle using MPC.
          *
          * Both are in between [-1, 1].
          *
          */
          auto res = mpc.Solve(state, coeffs);
          
          double steer_value;
          double throttle_value;
          
          // NOTE: Remember to divide by deg2rad(25) before you send the steering value back.
          // Otherwise the values will be in between [-deg2rad(25), deg2rad(25] instead of [-1, 1].
          steer_value = res.next_steering_angle() / deg2rad(25.0);
          throttle_value = res.next_throttle();

          cout << "MPC round done [cost=" << res.cost
               << ", cte=" << res.cte
               << ", steer=" << steer_value
               << ", throttle=" << throttle_value
               << "]" << endl;
          
          json msgJson;          
          msgJson["steering_angle"] = steer_value;
          msgJson["throttle"] = throttle_value;

          //Display the MPC predicted trajectory 
          vector<double> mpc_x_vals = res.predicted_xs;
          vector<double> mpc_y_vals = res.predicted_ys;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Green line
          // to_vehicle_coords(mpc_x_vals, mpc_y_vals, px, py, psi);
          msgJson["mpc_x"] = mpc_x_vals;
          msgJson["mpc_y"] = mpc_y_vals;

          //Display the waypoints/reference line                    
          vector<double> next_x_vals = ptsx;
          vector<double> next_y_vals = ptsy;
          
          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Yellow line
          msgJson["next_x"] = ptsx;
          msgJson["next_y"] = ptsy;

          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          // std::cout << msg << std::endl;
          
          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
          // SUBMITTING.
          this_thread::sleep_for(chrono::milliseconds(100));
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
