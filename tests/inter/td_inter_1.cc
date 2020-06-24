#include "../program_options.hpp"
#include "../common.hpp"

#include <crab/cg/cg_bgl.hpp>
#include <crab/analysis/graphs/sccg_bgl.hpp>

// For top-down inter-procedural analysis parameters
#include <crab/analysis/inter/top_down_inter_analyzer.hpp>

using namespace std;
using namespace crab::analyzer;
using namespace crab::cfg;
using namespace crab::cfg_impl;
using namespace crab::domain_impl;
using namespace crab::cg;

z_cfg_t* foo(variable_factory_t &vfac) {
  // Defining program variables
  z_var x(vfac["x"], crab::INT_TYPE, 32);
  z_var y(vfac["y"], crab::INT_TYPE, 32);
  z_var z(vfac["z"], crab::INT_TYPE, 32);
  
  function_decl<z_number, varname_t> decl("foo", {x}, {z});
  // entry and exit block
  z_cfg_t* cfg = new z_cfg_t("entry", "exit", decl);
  // adding blocks
  z_basic_block_t& entry = cfg->insert("entry");
  z_basic_block_t& exit   = cfg->insert("exit");
  // adding control flow
  entry >> exit;
  // adding statements
  entry.add(y, x, 1);
  exit.add(z , y , 2);
  exit.ret(z);
  return cfg;
}

z_cfg_t* rec1(variable_factory_t &vfac) {
  // Defining program variables
  z_var r(vfac["r"], crab::INT_TYPE, 32);
  z_var s(vfac["s"], crab::INT_TYPE, 32);
  z_var t(vfac["t"], crab::INT_TYPE, 32);
  
  function_decl<z_number, varname_t> decl("rec1", {s},{t});
  // entry and exit block
  z_cfg_t* cfg  = new z_cfg_t("entry", "exit", decl);
  // adding blocks
  z_basic_block_t& entry = cfg->insert("entry");
  z_basic_block_t& exit   = cfg->insert("exit");
  // adding control flow
  entry >> exit;
  // adding statements
  entry.sub(r, s, 1);
  exit.callsite("rec2", {t}, {r});
  exit.ret(t);
  return cfg;
}

z_cfg_t* rec2(variable_factory_t &vfac) {
  // Defining program variables
  z_var r(vfac["r1"], crab::INT_TYPE, 32);
  z_var s(vfac["s1"], crab::INT_TYPE, 32);
  z_var t(vfac["t1"], crab::INT_TYPE, 32);
  z_var a(vfac["a"], crab::INT_TYPE, 32);  
  
  function_decl<z_number, varname_t> decl("rec2", {s},{t});
  // entry and exit block
  z_cfg_t* cfg = new z_cfg_t("entry", "exit", decl);
  // adding blocks
  z_basic_block_t& entry = cfg->insert("entry");
  z_basic_block_t& exit   = cfg->insert("exit");
  // adding control flow
  entry >> exit;
  // adding statements
  entry.assign(a, 10);
  entry.sub(r, s, 1);
  exit.callsite("rec1", {t}, {r});
  //exit.callsite("foo", {t}, {t});
  exit.assertion(a >= 5);  
  exit.ret(t);
  return cfg;
}


z_cfg_t* bar(variable_factory_t &vfac) {
  // Defining program variables
  z_var a(vfac["a"], crab::INT_TYPE, 32);
  z_var x(vfac["x1"], crab::INT_TYPE, 32);
  z_var y(vfac["y1"], crab::INT_TYPE, 32);
  z_var w(vfac["w1"], crab::INT_TYPE, 32);
  
  function_decl<z_number, varname_t> decl("bar",{a},{y});
  // entry and exit block
  z_cfg_t* cfg = new z_cfg_t("entry", "exit", decl);
  // adding blocks
  z_basic_block_t& entry = cfg->insert("entry");
  z_basic_block_t& exit   = cfg->insert("exit");
  // adding control flow
  entry >> exit;
  // adding statements
  exit.callsite("foo", {y}, {x});
  exit.assertion(y >= 6);
  exit.assertion(y <= 17);
  entry.assign(x, a);
  entry.assign(w, 5);
  exit.ret(y);
  return cfg;
}

z_cfg_t* m(variable_factory_t &vfac)  {
  // Defining program variables
  z_var x(vfac["x2"], crab::INT_TYPE, 32);
  z_var x3(vfac["x3"], crab::INT_TYPE, 32);
  z_var x4(vfac["x4"], crab::INT_TYPE, 32);
  z_var x5(vfac["x5"], crab::INT_TYPE, 32);      
  z_var y(vfac["y2"], crab::INT_TYPE, 32);
  z_var y3(vfac["y3"], crab::INT_TYPE, 32);
  z_var y4(vfac["y4"], crab::INT_TYPE, 32);
  z_var y5(vfac["y5"], crab::INT_TYPE, 32);
  z_var y6(vfac["y6"], crab::INT_TYPE, 32);    
  z_var z(vfac["z2"], crab::INT_TYPE, 32);
  z_var z3(vfac["z3"], crab::INT_TYPE, 32);
  z_var u(vfac["__"], crab::INT_TYPE, 32);  
  z_var w(vfac["w2"], crab::INT_TYPE, 32);
  z_var res(vfac["res"], crab::INT_TYPE, 32);    
  
  function_decl<z_number, varname_t> decl("main", {}, {w});
				 
  // entry and exit block
  z_cfg_t* cfg = new z_cfg_t("entry", "exit", decl);
  // adding blocks
  z_basic_block_t& entry = cfg->insert("entry");
  z_basic_block_t& exit   = cfg->insert("exit");
  // adding control flow
  entry >> exit;
  // adding statements
  entry.assign(x, 3);
  entry.assign(x3, 4);
  entry.assign(x4, 5);
  entry.assign(x5, 6);  
  entry.callsite("bar", {y}, {x});
  entry.assertion(y == 6);  
  /////
  entry.callsite("rec1", {u}, {y});
  /////
  exit.add(z, y, 2);
  exit.callsite("bar", {y3}, {x});
  exit.assertion(y3 == 6);
  exit.add(z3, y3, z);
  exit.callsite("foo", {w}, {z3});
  exit.assertion(w == 17);   // provable only if we don't join calling contexts
  exit.callsite("bar", {y4}, {x3});
  exit.assertion(y4 == 7);
  exit.callsite("bar", {y5}, {x4});
  exit.assertion(y5 == 8);  
  exit.callsite("bar", {y6}, {x5});
  exit.assertion(y6 == 9);    
  exit.add(res, w, y4);
  exit.add(res, res, y5);
  exit.add(res, res, y6);
  exit.assertion(res == 41);
  exit.ret(res);
  return cfg;
}

typedef call_graph<z_cfg_ref_t> callgraph_t;
typedef call_graph_ref<callgraph_t> callgraph_ref_t;
typedef top_down_inter_analyzer_parameters<callgraph_ref_t> inter_params_t;

int main(int argc, char** argv) {
  bool stats_enabled = false;
  if(!crab_tests::parse_user_options(argc,argv,stats_enabled)) {
      return 0;
  }
  variable_factory_t vfac;
  z_cfg_t* t1 = foo(vfac);
  z_cfg_t* t2 = bar(vfac);
  z_cfg_t* t3 = rec1(vfac);
  z_cfg_t* t4 = rec2(vfac);
  z_cfg_t* t5 = m(vfac);

  crab::outs() << *t1 << "\n"
	       << *t2 << "\n"
	       << *t3 << "\n"
	       << *t4 << "\n"
	       << *t5 << "\n";

  vector<z_cfg_ref_t> cfgs({*t1, *t2, *t3, *t4, *t5});
  callgraph_t cg(cfgs);
  {
    z_dbm_domain_t init; 
    crab::outs() << "Running top-down inter-procedural analysis with "
		 << init.domain_name() << "\n";

    /////////////////////////////////////////    
    // it should prove all assertions
    /////////////////////////////////////////    
    inter_params_t params1;    
    td_inter_run(&cg, init, params1, true, false, false);
    /////////////////////////////////////////
    // it should not prove all assertions
    /////////////////////////////////////////    
    inter_params_t params2;
    params2.max_call_contexts = 3;  
    params2.checker_verbosity = 1;
    td_inter_run(&cg, init, params2, true, false, false);    
  }  
  {
    z_sdbm_domain_t init;     
    crab::outs() << "Running top-down inter-procedural analysis with "
		 << init.domain_name() << "\n";

    /////////////////////////////////////////    
    // it should prove all assertions
    /////////////////////////////////////////    
    inter_params_t params1;    
    td_inter_run(&cg, init, params1, true, false, false);
    /////////////////////////////////////////
    // it should not prove all assertions
    /////////////////////////////////////////    
    inter_params_t params2;
    params2.max_call_contexts = 3;  
    params2.checker_verbosity = 1;
    td_inter_run(&cg, init, params2, true, false, false);    
    
  }
#ifdef HAVE_APRON   
  {
    z_oct_apron_domain_t init;
    crab::outs() << "Running top-down inter-procedural analysis with "
		 << init.domain_name() << "\n";

    
    /////////////////////////////////////////    
    // it should prove all assertions
    /////////////////////////////////////////    
    inter_params_t params1;    
    td_inter_run(&cg, init, params1, true, false, false);
    /////////////////////////////////////////
    // it should not prove all assertions
    /////////////////////////////////////////    
    inter_params_t params2;
    params2.max_call_contexts = 3;  
    params2.checker_verbosity = 1;
    td_inter_run(&cg, init, params2, true, false, false);    

  }
#elif defined(HAVE_ELINA)
  {
    z_oct_elina_domain_t init;
    crab::outs() << "Running top-down inter-procedural analysis with "
		 << init.domain_name() << "\n";

    /////////////////////////////////////////    
    // it should prove all assertions
    /////////////////////////////////////////    
    inter_params_t params1;    
    td_inter_run(cg, init, params1, false);
    /////////////////////////////////////////
    // it should not prove all assertions
    /////////////////////////////////////////    
    inter_params_t params2;
    params2.max_call_contexts = 3;  
    params2.checker_verbosity = 1;
    td_inter_run(cg, init, params2, false);    
    
  }
#endif
  
  
  delete t1;
  delete t2;
  delete t3;
  delete t4;
  delete t5;

  return 0;
}
