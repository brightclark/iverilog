/*
 * Copyright (c) 2000 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#if !defined(WINNT) && !defined(macintosh)
#ident "$Id: elab_sig.cc,v 1.13 2001/05/25 02:21:34 steve Exp $"
#endif

# include  "Module.h"
# include  "PExpr.h"
# include  "PGate.h"
# include  "PTask.h"
# include  "PWire.h"
# include  "netlist.h"
# include  "netmisc.h"
# include  "util.h"

/*
 * This local function checks if a named signal is connected to a
 * port. It looks in the array of ports passed, for NetEIdent objects
 * within the port_t that have a matching name.
 */
static bool signal_is_in_port(const svector<Module::port_t*>&ports,
			      const string&name)
{
      for (unsigned idx = 0 ;  idx < ports.count() ;  idx += 1) {

	    Module::port_t*pp = ports[idx];
	      // Skip internally unconnected ports.
	    if (pp == 0)
		  continue;

	      // This port has an internal connection. In this case,
	      // the port has 0 or more NetEIdent objects concatenated
	      // together that form the port.
	    for (unsigned cc = 0 ;  cc < pp->expr.count() ;  cc += 1) {
		  assert(pp->expr[cc]);
		  if (pp->expr[cc]->name() == name)
			return true;
	    }
      }

      return false;
}

bool Module::elaborate_sig(Design*des, NetScope*scope) const
{
      bool flag = true;

	// Get all the explicitly declared wires of the module and
	// start the signals list with them.
      const map<string,PWire*>&wl = get_wires();

      for (map<string,PWire*>::const_iterator wt = wl.begin()
		 ; wt != wl.end()
		 ; wt ++ ) {

	    PWire*cur = (*wt).second;
	    cur->elaborate_sig(des, scope);

	      // If this wire is a signal of the module (as opposed to
	      // a port of a function) and is a port, then check that
	      // the module knows about it.
	    NetNet*sig = scope->find_signal(cur->name());
	    if (sig && (sig->scope() == scope)
		&& (cur->get_port_type() != NetNet::NOT_A_PORT)) {
		  string name = (*wt).first;

		  if (! signal_is_in_port(ports_, name)) {

			cerr << cur->get_line() << ": error: Signal "
			     << name << " has a declared direction "
			     << "but is not a port." << endl;
			des->errors += 1;
		  }
	    }

	      /* If the signal is an input and is also declared as a
		 reg, then report an error. */

	    if (sig && (sig->scope() == scope)
		&& (sig->port_type() == NetNet::PINPUT)
		&& (sig->type() == NetNet::REG)) {

		  cerr << cur->get_line() << ": error: "
		       << cur->name() << " in module "
		       << scope->module_name()
		       << " declared as input and as a reg type." << endl;
		  des->errors += 1;
	    }

	    if (sig && (sig->scope() == scope)
		&& (sig->port_type() == NetNet::PINOUT)
		&& (sig->type() == NetNet::REG)) {

		  cerr << cur->get_line() << ": error: "
		       << cur->name() << " in  module "
		       << scope->module_name()
		       << " declared as inout and as a reg type." << endl;
		  des->errors += 1;
	    }

      }

	// Get all the gates of the module and elaborate them by
	// connecting them to the signals. The gate may be simple or
	// complex. What we are looking for is gates that are modules
	// that can create scopes and signals.

      const list<PGate*>&gl = get_gates();

      for (list<PGate*>::const_iterator gt = gl.begin()
		 ; gt != gl.end()
		 ; gt ++ ) {

	    flag &= (*gt)->elaborate_sig(des, scope);
      }


      typedef map<string,PFunction*>::const_iterator mfunc_it_t;

      for (mfunc_it_t cur = funcs_.begin()
		 ; cur != funcs_.end() ;  cur ++) {
	    NetScope*fscope = scope->child((*cur).first);
	    if (scope == 0) {
		  cerr << (*cur).second->get_line() << ": internal error: "
		       << "Child scope for function " << (*cur).first
		       << " missing in " << scope->name() << "." << endl;
		  des->errors += 1;
		  continue;
	    }

	    (*cur).second->elaborate_sig(des, fscope);
      }


	// After all the wires are elaborated, we are free to
	// elaborate the ports of the tasks defined within this
	// module. Run through them now.

      typedef map<string,PTask*>::const_iterator mtask_it_t;

      for (mtask_it_t cur = tasks_.begin()
		 ; cur != tasks_.end() ;  cur ++) {
	    NetScope*tscope = scope->child((*cur).first);
	    assert(tscope);
	    (*cur).second->elaborate_sig(des, tscope);
      }

      return flag;
}

bool PGModule::elaborate_sig_mod_(Design*des, NetScope*scope,
				  Module*rmod) const
{
	// Missing module instance names have already been rejected.
      assert(get_name() != "");

      if (msb_) {
	    cerr << get_line() << ": sorry: Module instantiation arrays "
		  "are not yet supported." << endl;
	    des->errors += 1;
	    return false;
      }


	// I know a priori that the elaborate_scope created the scope
	// already, so just look it up as a child of the current scope.
      NetScope*my_scope = scope->child(get_name());
      assert(my_scope);

      return rmod->elaborate_sig(des, my_scope);
}

/*
 * A function definition exists within an elaborated module.
 */
void PFunction::elaborate_sig(Design*des, NetScope*scope) const
{
      string fname = scope->basename();
      assert(scope->type() == NetScope::FUNC);

      svector<NetNet*>ports (ports_? ports_->count()+1 : 1);

	/* Get the reg for the return value. I know the name of the
	   reg variable, and I know that it is in this scope, so look
	   it up directly. */
      ports[0] = scope->find_signal(scope->basename());
      if (ports[0] == 0) {
	    cerr << get_line() << ": internal error: function scope "
		 << scope->name() << " is missing return reg "
		 << fname << "." << endl;
	    scope->dump(cerr);
	    des->errors += 1;
	    return;
      }

      if (ports_)
	    for (unsigned idx = 0 ;  idx < ports_->count() ;  idx += 1) {

		    /* Parse the port name into the task name and the reg
		       name. We know by design that the port name is given
		       as two components: <func>.<port>. */

		  string pname = (*ports_)[idx]->name();
		  string ppath = parse_first_name(pname);

		  if (ppath != scope->basename()) {
			cerr << get_line() << ": internal error: function "
			     << "port " << (*ports_)[idx]->name()
			     << " has wrong name for function "
			     << scope->name() << "." << endl;
			des->errors += 1;
		  }

		  NetNet*tmp = scope->find_signal(pname);
		  if (tmp == 0) {
			cerr << get_line() << ": internal error: function "
			     << scope->name() << " is missing port "
			     << pname << "." << endl;
			scope->dump(cerr);
			des->errors += 1;
		  }

		  ports[idx+1] = tmp;
	    }


      NetFuncDef*def = new NetFuncDef(scope, ports);
      scope->set_func_def(def);
}

/*
 * A task definition is a scope within an elaborated module. When we
 * are elaborating signals, the scopes have already been created, as
 * have the reg objects that are the parameters of this task. The
 * elaborate_sig method of PTask is therefore left to connect the
 * signals to the ports of the NetTaskDef definition. We know for
 * certain that signals exist (They are in my scope!) so the port
 * binding is sure to work.
 */
void PTask::elaborate_sig(Design*des, NetScope*scope) const
{
      assert(scope->type() == NetScope::TASK);

      svector<NetNet*>ports (ports_? ports_->count() : 0);
      for (unsigned idx = 0 ;  idx < ports.count() ;  idx += 1) {

	      /* Parse the port name into the task name and the reg
		 name. We know by design that the port name is given
		 as two components: <task>.<port>. */

	    string pname = (*ports_)[idx]->name();
	    string ppath = parse_first_name(pname);
	    assert(pname != "");

	      /* check that the current scope really does have the
		 name of the first component of the task port name. Do
		 this by looking up the task scope in the parent of
		 the current scope. */
	    if (scope->parent()->child(ppath) != scope) {
		  cerr << "internal error: task scope " << ppath
		       << " not the same as scope " << scope->name()
		       << "?!" << endl;
		  return;
	    }

	      /* Find the signal for the port. We know by definition
		 that it is in the scope of the task, so look only in
		 the scope. */
	    NetNet*tmp = scope->find_signal(pname);

	    if (tmp == 0) {
		  cerr << get_line() << ": internal error: "
		       << "Could not find port " << pname
		       << " in scope " << scope->name() << endl;
		  scope->dump(cerr);
	    }

	    ports[idx] = tmp;
      }

      NetTaskDef*def = new NetTaskDef(scope->name(), ports);
      scope->set_task_def(def);
}

bool PGate::elaborate_sig(Design*des, NetScope*scope) const
{
      return true;
}

/*
 * Elaborate a source wire. The "wire" is the declaration of wires,
 * registers, ports and memories. The parser has already merged the
 * multiple properties of a wire (i.e. "input wire") so come the
 * elaboration this creates an object in the design that represent the
 * defined item.
 */
void PWire::elaborate_sig(Design*des, NetScope*scope) const
{
	/* The parser may produce hierarchical names for wires. I here
	   follow the scopes down to the base where I actually want to
	   elaborate the NetNet object. */
      string basename = name_;
      for (;;) {
	    string p = parse_first_name(basename);
	    if (basename == "") {
		  basename = p;
		  break;
	    }

	    scope = scope->child(p);
	    assert(scope);
      }

      const string path = scope->name();
      NetNet::Type wtype = type_;
      if (wtype == NetNet::IMPLICIT)
	    wtype = NetNet::WIRE;
      if (wtype == NetNet::IMPLICIT_REG)
	    wtype = NetNet::REG;

      unsigned wid = 1;
      long lsb = 0, msb = 0;

      assert(msb_.count() == lsb_.count());
      if (msb_.count()) {
	    svector<long>mnum (msb_.count());
	    svector<long>lnum (msb_.count());

	      /* There may be multiple declarations of ranges, because
		 the symbol may have its range declared in i.e. input
		 and reg declarations. Calculate *all* the numbers
		 here. I will resolve the values later. */

	    for (unsigned idx = 0 ;  idx < msb_.count() ;  idx += 1) {

		  NetEConst*tmp;
		  NetExpr*texpr = elab_and_eval(des, scope, msb_[idx]);

		  tmp = dynamic_cast<NetEConst*>(texpr);
		  if (tmp == 0) {
			cerr << msb_[idx]->get_line() << ": error: "
			      "Unable to evaluate constant expression ``" <<
			      *msb_[idx] << "''." << endl;
			des->errors += 1;
			return;
		  }

		  mnum[idx] = tmp->value().as_long();
		  delete texpr;

		  texpr = elab_and_eval(des, scope, lsb_[idx]);
		  tmp = dynamic_cast<NetEConst*>(texpr);
		  if (tmp == 0) {
			cerr << msb_[idx]->get_line() << ": error: "
			      "Unable to evaluate constant expression ``" <<
			      *lsb_[idx] << "''." << endl;
			des->errors += 1;
			return;
		  }

		  lnum[idx] = tmp->value().as_long();
		  delete texpr;

	    }

	      /* Make sure all the values for msb and lsb match by
		 value. If not, report an error. */
	    for (unsigned idx = 1 ;  idx < msb_.count() ;  idx += 1) {
		  if ((mnum[idx] != mnum[0]) || (lnum[idx] != lnum[0])) {
			cerr << get_line() << ": error: Inconsistent width, "
			      "[" << mnum[idx] << ":" << lnum[idx] << "]"
			      " vs. [" << mnum[0] << ":" << lnum[0] << "]"
			      " for signal ``" << basename << "''" << endl;
			des->errors += 1;
			return;
		  }
	    }

	    lsb = lnum[0];
	    msb = mnum[0];
	    if (mnum[0] > lnum[0])
		  wid = mnum[0] - lnum[0] + 1;
	    else
		  wid = lnum[0] - mnum[0] + 1;


      }

      if (lidx_ || ridx_) {
	    assert(lidx_ && ridx_);

	      // If the register has indices, then this is a
	      // memory. Create the memory object.
	    verinum*lval = lidx_->eval_const(des, path);
	    verinum*rval = ridx_->eval_const(des, path);

	    if ((lval == 0) || (rval == 0)) {
		  cerr << get_line() << ": internal error: There is "
		       << "a problem evaluating indices for ``"
		       << basename << "''." << endl;
		  des->errors += 1;
		  return;
	    }

	    assert(lval);
	    assert(rval);

	    long lnum = lval->as_long();
	    long rnum = rval->as_long();
	    delete lval;
	    delete rval;
	    NetMemory*sig = new NetMemory(scope, path+"."+basename,
					  wid, lnum, rnum);

      } else {

	    NetNet*sig = new NetNet(scope, path + "." +basename, wtype, msb, lsb);
	    sig->set_line(*this);
	    sig->port_type(port_type_);
	    sig->set_signed(get_signed());
	    sig->set_attributes(attributes);
      }
}

/*
 * $Log: elab_sig.cc,v $
 * Revision 1.13  2001/05/25 02:21:34  steve
 *  Detect input and input ports declared as reg.
 *
 * Revision 1.12  2001/02/17 05:15:33  steve
 *  Allow task ports to be given real types.
 *
 * Revision 1.11  2001/02/10 20:29:39  steve
 *  In the context of range declarations, use elab_and_eval instead
 *  of the less robust eval_const methods.
 *
 * Revision 1.10  2001/01/13 22:20:08  steve
 *  Parse parameters within nested scopes.
 *
 * Revision 1.9  2001/01/07 07:00:31  steve
 *  Detect port direction attached to non-ports.
 *
 * Revision 1.8  2001/01/04 04:47:51  steve
 *  Add support for << is signal indices.
 *
 * Revision 1.7  2000/12/11 00:31:43  steve
 *  Add support for signed reg variables,
 *  simulate in t-vvm signed comparisons.
 *
 * Revision 1.6  2000/12/04 17:37:04  steve
 *  Add Attrib class for holding NetObj attributes.
 *
 * Revision 1.5  2000/11/20 00:58:40  steve
 *  Add support for supply nets (PR#17)
 *
 * Revision 1.4  2000/09/07 22:37:48  steve
 *  ack, detect when lval fails.
 *
 * Revision 1.3  2000/07/30 18:25:43  steve
 *  Rearrange task and function elaboration so that the
 *  NetTaskDef and NetFuncDef functions are created during
 *  signal enaboration, and carry these objects in the
 *  NetScope class instead of the extra, useless map in
 *  the Design class.
 *
 * Revision 1.2  2000/07/14 06:12:57  steve
 *  Move inital value handling from NetNet to Nexus
 *  objects. This allows better propogation of inital
 *  values.
 *
 *  Clean up constant propagation  a bit to account
 *  for regs that are not really values.
 *
 * Revision 1.1  2000/05/02 16:27:38  steve
 *  Move signal elaboration to a seperate pass.
 *
 */

