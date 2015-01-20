/*

  TODO
  auf der node.js console erscheint manchmal (test mit 1000 Anfragen) die Ausgabe von SCIP:
  pressed CTRL-C 1 times (5 times for forcing termination)
  Ist SCIP nicht korrekt beendet worden?


*/

/* define SCIP_DEBUG to enable debug messages */
//#define SCIP_DEBUG

#include <node.h>
#include <v8.h>
#include <assert.h>
#include <string>
#include <vector>    
#include "scip/scip.h"
#include "scip/scipdefplugins.h"
#include "scip/cons_linear.h"

using namespace v8;

class LP
{
  public:
    SCIP_RETCODE scipInit(Local<Object>&  variables, Local<Array>& constraints);
    SCIP_RETCODE scipExit(); // clean if init fails
  
    SCIP* scip;
    SCIP_RETCODE retCode;
    std::vector<SCIP_VAR*> scipvars;
    uint32_t varslength;
    const char* msg;
};

/** error messages handling */
static
SCIP_DECL_ERRORPRINTING(errorHandlingAsync)
{
   LP* lp;

   lp = (LP*)(data);

   lp->msg = msg;
}

SCIP_RETCODE LP::scipInit(Local<Object>&  variables, Local<Array>& constraints)
{
  /* create SCIP environment */
  SCIP_CALL( SCIPcreate(&scip) );
  SCIPdebugMessage("created SCIP environment\n");

  /* set error message handling method */
  SCIPmessageSetErrorPrinting(errorHandlingAsync, (void*)this);

  /* disable output */
#ifndef SCIP_DEBUG
  SCIP_CALL( SCIPsetIntParam(scip, "display/verblevel", 0) );
#endif

  /* include all default plugins */
  SCIP_CALL( SCIPincludeDefaultPlugins(scip) );

  /* create (empty) problem */
  SCIP_CALL( SCIPcreateProbBasic(scip, "solid") );
  SCIPdebugMessage("created (empty) problem\n");

  /* sets up problem */
  Local<Array> varprops = variables->GetPropertyNames();
  varslength = varprops->Length();
  scipvars.resize(varslength);
  std::string varnames[varslength];

  /* create variables */
  for (uint32_t i = 0; i < varslength; i++) {

    const Local<Value> key = varprops->Get(i);
    String::Utf8Value keystr(key->ToString());
    std::string varname = std::string(*keystr);
    const Local<Value> value = variables->Get(key);

    SCIP_VAR* scipvar;

    /* create a continuous variable */
    SCIPcreateVarBasic(scip, &scipvar, varname.c_str(), 0.0, SCIPinfinity(scip), value->NumberValue(), SCIP_VARTYPE_CONTINUOUS);

    /* add variable to problem */
    SCIPaddVar(scip, scipvar);

    // arrays, um bei der constraint erzeugung var. namen und SCIP_VAR pointer zuzuornen. Schlechte Konstruktion
    scipvars[i] = scipvar;
    varnames[i] = varname;
  }
  SCIPdebugMessage("added %d variables\n", varslength);

  const uint32_t conslength = constraints->Length();

  /* create linear constraints */
  for (uint32_t i = 0; i < conslength; i++) {

    const Local<Object> con = constraints->Get(i)->ToObject();
    const Local<Array> conprops = con->GetPropertyNames();
    const uint32_t conpropslength = conprops->Length();

    SCIP_CONS *scipcons;
    String::Utf8Value keystr(con->Get(String::New("name"))->ToString());
    std::string consname = std::string(*keystr);
    SCIP_Real lhs = con->Get(String::New("lhs"))->NumberValue();
    SCIP_Real rhs = con->Get(String::New("rhs"))->NumberValue();

    if (lhs < rhs)
       lhs = -SCIPinfinity(scip);
    else if (lhs > rhs) {
       lhs = rhs;
       rhs = SCIPinfinity(scip);
    }

    /* create linear constraint with (zero variables) */
    SCIP_CALL( SCIPcreateConsBasicLinear(scip, &scipcons, consname.c_str(), 0, NULL, NULL, lhs, rhs) );

    // iterate over objectproperties
    for (uint32_t j = 0; j < conpropslength; j++) {

      const Local<Value> key = conprops->Get(j);
      String::Utf8Value keystr(key->ToString());
      std::string varname = std::string(*keystr);
      const Local<Value> value = con->Get(key);

      // besserer Lösung finden / JSON Struktur ändern
      if (varname.compare("name") == 0 || varname.compare("lhs") == 0 || varname.compare("rhs") == 0)
        continue;

      /* add variable to constraint */
      for (uint32_t k = 0; k < varslength; k++) {

         if (varname.compare(varnames[k]) == 0) {
            SCIP_CALL( SCIPaddCoefLinear(scip, scipcons, scipvars[k], value->NumberValue()) );
            break;
         }
      }
    }

    /* add constraint to problem */
    SCIP_CALL( SCIPaddCons(scip, scipcons) );

    /* release constraints (meaning SCIP should take care of deleting it if it is not needed anymore) */
    SCIP_CALL( SCIPreleaseCons(scip, &scipcons) );
  }
  SCIPdebugMessage("added %d constraints\n", conslength);

  /* set a time limit (ms) to ensure that SCIP comes back */
  SCIP_CALL( SCIPsetRealParam(scip, "limits/time", 10000) );

  /* write to file in LP format (only if SCIP_DEBUG is defined */
  SCIPdebug( SCIP_CALL( SCIPwriteOrigProblem(scip, "solid.lp", "lp", FALSE) ) );

  return SCIP_OKAY;
}

SCIP_RETCODE LP::scipExit()
{
  /* release variables */
  for (uint32_t i = 0; i < scipvars.size(); i++)
    SCIP_CALL( SCIPreleaseVar(scip, &scipvars[i]) );

  /* free problem */
  SCIP_CALL( SCIPfree(&scip) );
  SCIPdebugMessage("freed SCIP environment\n");

  return SCIP_OKAY;
}

struct LPbaton {
  LP* lp;
  Persistent<Function> cb;
};

void scipSolve(uv_work_t *req)
{
  LPbaton *baton = (LPbaton *)req->data;
  /* solve the problem (includes presolving)
      TODO: save w/o SCIP_CALL?
  */
  baton->lp->retCode = SCIPsolve(baton->lp->scip);
  SCIPdebugMessage("solved problem\n");
}

SCIP_RETCODE afterScipSolve(uv_work_t *req)
{
  LPbaton *baton = (LPbaton *)req->data; 
  SCIP* scip = baton->lp->scip;
  SCIP_VAR** scipvars = &baton->lp->scipvars[0];
  uint32_t varslength = baton->lp->varslength;

  Handle<Object> ret = Object::New();

  /* check if a solution is available */
  if ( SCIPgetNSols(scip) > 0 ) {

    SCIP_SOL* sol = SCIPgetBestSol(scip);
    uint32_t v;

    SCIPdebugMessage("found feasible solution\n");
    SCIPdebug( SCIP_CALL( SCIPprintSol(scip, sol, NULL, FALSE) ) );

    /* get best solution */
    sol = SCIPgetBestSol(scip);
    assert(sol != NULL);

    /* store objective value */
    ret->Set(String::New("objValue"), Number::New(SCIPgetSolOrigObj(scip, sol)));

    /* store solving time (in seconds)*/
    ret->Set(String::New("solTime"), Number::New(SCIPgetSolvingTime(scip)));

    /* loop over all variables (which life in the original space) and collect the solution values */
    for( v = 0; v < varslength; ++v ) {

      SCIP_VAR* var;
      SCIP_Real solval;
      const char* name;

      var = scipvars[v];
      assert(var != NULL);

      solval = SCIPgetVarSol(scip, var);
      name = SCIPvarGetName(var);

      /* evaluate solution value */
      if ( solval == SCIP_UNKNOWN )
        ret->Set(String::New(name), String::New("unkown"));
      else if ( SCIPisInfinity(scip, solval) )
        ret->Set(String::New(name), String::New("+infinity"));
      else if ( SCIPisInfinity(scip, -solval) )
        ret->Set(String::New(name), String::New("-infinity"));
      else {
        solval = double(int(solval * 1000) / 1000.0);
        ret->Set(String::New(name), Number::New(solval));
      }
    }
  }
  else if( SCIPgetStatus(scip) == SCIP_STATUS_INFEASIBLE ) {
     /* problem is infeasible */
     SCIPdebugMessage("problem is infeasible\n");
     ret->Set(String::New("error"), String::New("problem is infeasible"));
  }
  else {
     /* no solution is available and problem is not solved yet */
    ret->Set(String::New("error"), String::New("no solution is available and problem is not solved yet"));
  }

  /* release variables */
  for (uint32_t i = 0; i < varslength; i++)
    SCIP_CALL( SCIPreleaseVar(scip, &scipvars[i]) );

  /* free problem */
  SCIP_CALL( SCIPfree(&scip) );
  SCIPdebugMessage("freed SCIP environment\n");

  Handle<Value> argv[] = { ret };
  baton->cb->Call(Context::GetCurrent()->Global(), 1, argv);
  baton->cb.Dispose();  

  delete baton->lp;
  delete baton;
  delete req;

  return SCIP_OKAY;
}

/** error messages handling */
static
SCIP_DECL_ERRORPRINTING(errorHandlingSync)
{
   Handle<Object>* ret;

   ret = (Handle<Object>*)(data);

   (*ret)->Set(String::New("error"), String::New(msg));
}

/** runs SCIP synchronous and returns error code */
SCIP_RETCODE runScip(
   Local<Object>&        variables,     /**< variables input */
   Local<Array>&         constraints,   /**< constraint input */
   Handle<Object>&       ret            /**< object to collect store solution */
   )
{
  SCIP* scip;

  /* create SCIP environment */
  SCIP_CALL( SCIPcreate(&scip) );
  SCIPdebugMessage("created SCIP environment\n");

  /* set error message handling method */
  SCIPmessageSetErrorPrinting(errorHandlingSync, (void*) &ret);

  /* disable output */
#ifndef SCIP_DEBUG
  SCIP_CALL( SCIPsetIntParam(scip, "display/verblevel", 0) );
#endif

  /* include all default plugins */
  SCIP_CALL( SCIPincludeDefaultPlugins(scip) );

  /* create (empty) problem */
  SCIP_CALL( SCIPcreateProbBasic(scip, "solid") );
  SCIPdebugMessage("created (empty) problem\n");

  /* sets up problem */
  Local<Array> varprops = variables->GetPropertyNames();
  uint32_t varslength = varprops->Length();
  SCIP_VAR* scipvars[varslength];
  std::string varnames[varslength];

  /* create variables */
  for (uint32_t i = 0; i < varslength; i++) {

    const Local<Value> key = varprops->Get(i);
    String::Utf8Value keystr(key->ToString());
    std::string varname = std::string(*keystr);
    const Local<Value> value = variables->Get(key);

    SCIP_VAR* scipvar;

    /* create a continuous variable */
    SCIPcreateVarBasic(scip, &scipvar, varname.c_str(), 0.0, SCIPinfinity(scip), value->NumberValue(), SCIP_VARTYPE_CONTINUOUS);

    /* add variable to problem */
    SCIPaddVar(scip, scipvar);

    // arrays, um bei der constraint erzeugung var. namen und SCIP_VAR pointer zuzuornen. Schlechte Konstruktion
    scipvars[i] = scipvar;
    varnames[i] = varname;
  }
  SCIPdebugMessage("added %d variables\n", varslength);

  const uint32_t conslength = constraints->Length();

  /* create linear constraints */
  for (uint32_t i = 0; i < conslength; i++) {

    const Local<Object> con = constraints->Get(i)->ToObject();
    const Local<Array> conprops = con->GetPropertyNames();
    const uint32_t conpropslength = conprops->Length();

    SCIP_CONS *scipcons;
    String::Utf8Value keystr(con->Get(String::New("name"))->ToString());
    std::string consname = std::string(*keystr);
    SCIP_Real lhs = con->Get(String::New("lhs"))->NumberValue();
    SCIP_Real rhs = con->Get(String::New("rhs"))->NumberValue();

    if (lhs < rhs)
       lhs = -SCIPinfinity(scip);
    else if (lhs > rhs) {
       lhs = rhs;
       rhs = SCIPinfinity(scip);
    }

    /* create linear constraint with (zero variables) */
    SCIP_CALL( SCIPcreateConsBasicLinear(scip, &scipcons, consname.c_str(), 0, NULL, NULL, lhs, rhs) );

    // iterate over objectproperties
    for (uint32_t j = 0; j < conpropslength; j++) {

      const Local<Value> key = conprops->Get(j);
      String::Utf8Value keystr(key->ToString());
      std::string varname = std::string(*keystr);
      const Local<Value> value = con->Get(key);

      // besserer Lösung finden / JSON Struktur ändern
      if (varname.compare("name") == 0 || varname.compare("lhs") == 0 || varname.compare("rhs") == 0)
        continue;

      /* add variable to constraint */
      for (uint32_t k = 0; k < varslength; k++) {

         if (varname.compare(varnames[k]) == 0) {
            SCIP_CALL( SCIPaddCoefLinear(scip, scipcons, scipvars[k], value->NumberValue()) );
            break;
         }
      }
    }

    /* add constraint to problem */
    SCIP_CALL( SCIPaddCons(scip, scipcons) );

    /* release constraints (meaning SCIP should take care of deleting it if it is not needed anymore) */
    SCIP_CALL( SCIPreleaseCons(scip, &scipcons) );
  }
  SCIPdebugMessage("added %d constraints\n", conslength);

  /* set a time limit (ms) to ensure that SCIP comes back */
  SCIP_CALL( SCIPsetRealParam(scip, "limits/time", 10000) );

  /* write to file in LP format (only if SCIP_DEBUG is defined */
  SCIPdebug( SCIP_CALL( SCIPwriteOrigProblem(scip, "solid.lp", "lp", FALSE) ) );

  /* solve the problem (includes presolving) */
  SCIP_CALL( SCIPsolve(scip) );
  SCIPdebugMessage("solved problem\n");

  /* check if a solution is available */
  if ( SCIPgetNSols(scip) > 0 ) {

    SCIP_SOL* sol = SCIPgetBestSol(scip);
    uint32_t v;

    SCIPdebugMessage("found feasible solution\n");
    SCIPdebug( SCIP_CALL( SCIPprintSol(scip, sol, NULL, FALSE) ) );

    /* get best solution */
    sol = SCIPgetBestSol(scip);
    assert(sol != NULL);

    /* store objective value */
    ret->Set(String::New("objValue"), Number::New(SCIPgetSolOrigObj(scip, sol)));

    /* store solving time (in seconds)*/
    ret->Set(String::New("solTime"), Number::New(SCIPgetSolvingTime(scip)));

    /* loop over all variables (which life in the original space) and collect the solution values */
    for( v = 0; v < varslength; ++v ) {

      SCIP_VAR* var;
      SCIP_Real solval;
      const char* name;

      var = scipvars[v];
      assert(var != NULL);

      solval = SCIPgetVarSol(scip, var);
      name = SCIPvarGetName(var);

      /* evaluate solution value */
      if ( solval == SCIP_UNKNOWN )
        ret->Set(String::New(name), String::New("unkown"));
      else if ( SCIPisInfinity(scip, solval) )
        ret->Set(String::New(name), String::New("+infinity"));
      else if ( SCIPisInfinity(scip, -solval) )
        ret->Set(String::New(name), String::New("-infinity"));
      else {
        solval = double(int(solval * 1000) / 1000.0);
        ret->Set(String::New(name), Number::New(solval));
      }
    }
  }
  else if( SCIPgetStatus(scip) == SCIP_STATUS_INFEASIBLE ) {
     /* problem is infeasible */
     SCIPdebugMessage("problem is infeasible\n");
     ret->Set(String::New("error"), String::New("problem is infeasible"));
  }
  else {
     /* no solution is available and problem is not solved yet */
    ret->Set(String::New("error"), String::New("no solution is available and problem is not solved yet"));
  }

  /* release variables */
  for (uint32_t i = 0; i < varslength; i++)
    SCIP_CALL( SCIPreleaseVar(scip, &scipvars[i]) );

  /* free problem */
  SCIP_CALL( SCIPfree(&scip) );
  SCIPdebugMessage("freed SCIP environment\n");

  return SCIP_OKAY;
}

Handle<Value> run(const Arguments &args) {

  HandleScope scope;
  Handle<Object> ret = Object::New();

  Local<Object> variables = Local<Object>::Cast(args[0]);
  Local<Array> constraints = Local<Array>::Cast(args[1]);

  SCIP_RETCODE retcode;

  if (!variables->IsObject() || !constraints->IsArray()) {
    ret->Set(String::New("error"), String::New("no valid lp JSON"));
  } else {
    /* run SCIP */
    retcode = runScip(variables, constraints, ret);

    /* evaluate retrun code of the SCIP process */
    if( retcode != SCIP_OKAY ) {
       /* write error back trace */
       SCIPprintError(retcode);
    }
  }

  return scope.Close(ret);
}

Handle<Value> runAsync(const Arguments& args) {

  HandleScope scope;

  Local<Object> variables = Local<Object>::Cast(args[0]);
  Local<Array> constraints = Local<Array>::Cast(args[1]);
  Persistent<Function> cb = Persistent<Function>::New(Local<Function>::Cast(args[2]));

  LP *lp = new LP;
  SCIP_RETCODE init = lp->scipInit(variables, constraints);

  if (init == SCIP_OKAY) {

    LPbaton *baton = new LPbaton;
    baton->lp = lp;
    baton->cb = cb;

    // create an async work token
    uv_work_t *req = new uv_work_t;

    // assign our data structure that will be passed around
    req->data = baton;

    // pass the work token to libuv to be run when a
    // worker-thread is available to
    uv_queue_work(
      uv_default_loop(),
      req,                              // work token
      scipSolve,                        // work function
      (uv_after_work_cb)afterScipSolve  // function to run when complete
    );

  } else {

    lp->scipExit();

    Handle<Object> ret = Object::New();
    ret->Set(String::New("error"), String::New(lp->msg));
    Handle<Value> argv[] = { ret };
    cb->Call(Context::GetCurrent()->Global(), 1, argv);
    cb.Dispose();

    delete lp;
  }

  return scope.Close(Undefined());
}

extern "C" {
  void init(Handle<Object> target) {
    
    target->Set(String::NewSymbol("run"), FunctionTemplate::New(run)->GetFunction());
    target->Set(String::NewSymbol("runAsync"), FunctionTemplate::New(runAsync)->GetFunction());
    
  }

  NODE_MODULE(scip_node_bindings, init)
}
