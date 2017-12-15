#pragma once 

/* 
 * Build a CFG to interface with the abstract domains and fixpoint
 * iterators.
 * 
 * All the CFG statements are strongly typed. However, only variables
 * need to be typed. The types of constants can be inferred from the
 * context since they always appear together with at least one
 * variable. Types form a **flat** lattice consisting of:
 * 
 * - booleans,
 * - integers,
 * - reals, 
 * - pointers, 
 * - array of booleans,
 * - array of integers, 
 * - array of reals, and 
 * - array of pointers.
 * 
 * Crab CFG supports the modelling of:
 * 
 *   - arithmetic operations over integers or reals,
 *   - boolean operations,
 *   - C-like pointers, 
 *   - uni-dimensional arrays of booleans, integers or pointers
 *     (useful for C-like arrays and heap abstractions),
 *   - and functions 
 * 
 * Important notes: 
 * 
 * - Objects of the class cfg are not copyable. Instead, we provide a
 *   class cfg_ref that wraps cfg references into copyable and
 *   assignable objects.
 * 
 * Limitations:
 *
 * - The CFG language does not allow to express floating point
 *   operations.
 * 
 */

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/iterator/indirect_iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/noncopyable.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include <crab/common/types.hpp>
#include <crab/common/bignums.hpp>
#include <crab/domains/linear_constraints.hpp>
#include <crab/domains/intervals.hpp>
#include <crab/domains/discrete_domains.hpp>

#include <functional> // for wrapper_reference

namespace crab {

  namespace cfg_impl  {
     // To convert a basic block label to a string
     template< typename T >
     inline std::string get_label_str(T e);
  } 

  namespace cfg {

    // The values must be such that NUM <= PTR <= ARR
    enum tracked_precision { NUM = 0, PTR = 1, ARR = 2 };
  
    enum stmt_code {
      UNDEF = 0,
      // numerical
      BIN_OP = 20, ASSIGN = 21, ASSUME = 22, UNREACH = 23, SELECT = 24,
      ASSERT = 25,
      // arrays 
      ARR_INIT = 30, ARR_ASSUME = 31, 
      ARR_STORE = 32, ARR_LOAD = 33, ARR_ASSIGN = 34,
      // pointers
      PTR_LOAD = 40, PTR_STORE = 41, PTR_ASSIGN = 42, 
      PTR_OBJECT = 43, PTR_FUNCTION = 44, PTR_NULL=45, 
      PTR_ASSUME = 46, PTR_ASSERT = 47,
      // functions calls
      CALLSITE = 50, RETURN = 51,
      // integers/arrays/pointers/boolean
      HAVOC = 60,
      // boolean
      BOOL_BIN_OP = 70, BOOL_ASSIGN_CST = 71, BOOL_ASSIGN_VAR = 72,
      BOOL_ASSUME = 73, BOOL_SELECT = 74, BOOL_ASSERT = 75,
      // casts
      INT_CAST = 80
    }; 

    template<typename Number, typename VariableName>
    inline crab_os& operator<< (crab_os &o, 
                                const ikos::linear_expression<Number, VariableName> &e)
    {
      auto tmp (e);
      tmp.write (o);
      return o;
    }

    template<typename Number, typename VariableName>
    inline crab_os& operator<<(crab_os &o, 
			       const ikos::linear_constraint<Number, VariableName> &c)
    {
      auto tmp (c);
      tmp.write (o);
      return o;
    }
    
    template<typename Number, typename VariableName>
    class Live
    {
      
     public:

      typedef ikos::variable<Number, VariableName> variable_t;

     private:
      
      typedef std::vector <variable_t> live_set_t;

     public:
      
      typedef typename live_set_t::const_iterator  const_use_iterator;
      typedef typename live_set_t::const_iterator  const_def_iterator;
      
     private:

      live_set_t m_uses;
      live_set_t m_defs;

      void add (live_set_t & s, variable_t v)
      {
        auto it = find (s.begin (), s.end (), v);
        if (it == s.end ()) s.push_back (v);
      }
      
     public:
      
      Live() { }
      
      void add_use(const variable_t v){ add (m_uses,v);}
      void add_def(const variable_t v){ add (m_defs,v);}
      
      const_use_iterator uses_begin() const { return m_uses.begin (); }
      const_use_iterator uses_end()   const { return m_uses.end (); }
      const_use_iterator defs_begin() const { return m_defs.begin (); }
      const_use_iterator defs_end()   const { return m_defs.end (); }

      friend crab_os& operator<<(crab_os &o, const Live<Number,VariableName> &live) {
        o << "Use={"; 
        for (auto const& v: boost::make_iterator_range (live.uses_begin (), 
                                                        live.uses_end ()))
          o << v << ",";
        o << "} Def={"; 
        for (auto const& v: boost::make_iterator_range (live.defs_begin (), 
                                                        live.defs_end ()))
          o << v << ",";
        o << "}";
        return o;
      }
    };

    struct debug_info {
      
      std::string m_file;
      int m_line;
      int m_col;
      
      debug_info ():
          m_file (""), m_line (-1), m_col (-1) { }

      debug_info (std::string file, unsigned line, unsigned col):
          m_file (file), m_line (line), m_col (col) { }
      
      bool operator<(const debug_info& other) const {
        return (m_file < other.m_file && 
                m_line < other.m_line && 
                m_col < other.m_col);
      }

      bool operator==(const debug_info& other) const {
        return (m_file == other.m_file && 
                m_line == other.m_line && 
                m_col == other.m_col);
      }

      bool has_debug  () const {
        return ((m_file != "") && (m_line >= 0) && (m_col >= 0));
      }

      void write (crab_os&o) const {
        o << "File  : " << m_file << "\n"
          << "Line  : " << m_line  << "\n" 
          << "Column: " << m_col << "\n";
      }
    };

    inline crab_os& operator<<(crab_os& o, const debug_info& l) {
      l.write (o);
      return o;
    }

    template<typename Number, typename VariableName>
    struct statement_visitor;
  
    template<class Number, class VariableName>
    class statement
    {
      
     public:
      typedef Live<Number, VariableName> live_t ;
      
     protected:
      live_t m_live;
      stmt_code m_stmt_code;
      debug_info m_dbg_info;

      statement (stmt_code code = UNDEF,
                 debug_info dbg_info = debug_info ()): 
          m_stmt_code (code),
          m_dbg_info (dbg_info) { }

     public:
      
      bool is_bin_op () const { 
        return (m_stmt_code == BIN_OP); 
      }
      bool is_assign () const { 
        return (m_stmt_code == ASSIGN); 
      }
      bool is_assume () const { 
        return (m_stmt_code == ASSUME); 
      }
      bool is_select () const { 
        return (m_stmt_code == SELECT); 
      }
      bool is_assert () const { 
        return (m_stmt_code == ASSERT); 
      }
      bool is_int_cast () const { 
        return (m_stmt_code == INT_CAST); 
      }
      bool is_return () const { 
        return m_stmt_code == RETURN; 
      }
      bool is_arr_read () const { 
        return (m_stmt_code == ARR_LOAD);
      }
      bool is_arr_write () const { 
        return (m_stmt_code == ARR_STORE); 
      }
      bool is_arr_assign () const { 
        return (m_stmt_code == ARR_ASSIGN); 
      }
      bool is_ptr_read () const {
        return (m_stmt_code == PTR_LOAD); 
      }
      bool is_ptr_write () const { 
        return (m_stmt_code == PTR_STORE); 
      }
      bool is_ptr_null () const { 
        return (m_stmt_code == PTR_NULL); 
      }
      bool is_ptr_assume () const { 
        return (m_stmt_code == PTR_ASSUME); 
      }
      bool is_ptr_assert () const { 
        return (m_stmt_code == PTR_ASSERT); 
      }
      bool is_bool_bin_op () const { 
        return (m_stmt_code == BOOL_BIN_OP); 
      }
      bool is_bool_assign_cst () const { 
        return (m_stmt_code == BOOL_ASSIGN_CST); 
      }
      bool is_bool_assign_var () const { 
        return (m_stmt_code == BOOL_ASSIGN_VAR); 
      }      
      bool is_bool_assume () const { 
        return (m_stmt_code == BOOL_ASSUME); 
      }
      bool is_bool_assert () const { 
        return (m_stmt_code == BOOL_ASSERT); 
      }      
      bool is_bool_select () const { 
        return (m_stmt_code == BOOL_SELECT); 
      }
      
     public:
      
      const live_t& get_live() const { return m_live; }

      const debug_info& get_debug_info () const { return m_dbg_info; }

      virtual void accept(statement_visitor<Number, VariableName> *) = 0;
      
      virtual void write(crab_os& o) const = 0 ;
      
      virtual boost::shared_ptr<statement <Number,VariableName> > clone () const = 0;
      
      virtual ~statement() { }
      
      friend crab_os& operator <<(crab_os&o, 
                                  const statement<Number,VariableName> &s)
      {
        s.write (o);
        return o;
      }
      
    }; 
  
    /*
      Numerical statements 
    */

    template< class Number, class VariableName>
    class binary_op: public statement <Number,VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;      
      typedef ikos::variable< Number, VariableName > variable_t;
      typedef ikos::linear_expression< Number, VariableName > linear_expression_t;
      
     private:
      
      variable_t          m_lhs;
      binary_operation_t  m_op;
      linear_expression_t m_op1;
      linear_expression_t m_op2;
      
     public:
      
      binary_op (variable_t lhs, 
		 binary_operation_t op, 
		 linear_expression_t op1, 
		 linear_expression_t op2,
		 debug_info dbg_info = debug_info ())
  	: statement_t (BIN_OP, dbg_info),
	  m_lhs(lhs), m_op(op), m_op1(op1), m_op2(op2) {
        this->m_live.add_def (m_lhs);
        for (auto v: m_op1.variables()){ this->m_live.add_use (v); }         
        for (auto v: m_op2.variables()){ this->m_live.add_use (v); }         
      }
      
      variable_t lhs () const { return m_lhs; }
      
      binary_operation_t op () const { return m_op; }
      
      linear_expression_t left () const { return m_op1; }
      
      linear_expression_t right () const { return m_op2; }
      
      virtual void accept(statement_visitor <Number,VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef binary_op<Number, VariableName> binary_op_t;
        return boost::static_pointer_cast<statement_t, binary_op_t>
	  (boost::make_shared<binary_op_t>(m_lhs, m_op, m_op1, m_op2,
					   this->m_dbg_info));
      }
      
      virtual void write (crab_os& o) const
      {
        o << m_lhs << " = " << m_op1 << m_op << m_op2;
        return;
      }
    }; 

    template< class Number, class VariableName>
    class assignment: public statement<Number, VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;            
      typedef ikos::variable<Number, VariableName> variable_t;
      typedef ikos::linear_expression<Number, VariableName> linear_expression_t;
      
     private:
      
      variable_t          m_lhs;
      linear_expression_t m_rhs;
      
     public:
      
      assignment (variable_t lhs, linear_expression_t rhs)
	: statement_t (ASSIGN), m_lhs(lhs), m_rhs(rhs) 
      {
        this->m_live.add_def (m_lhs);
        for(auto v: m_rhs.variables()) 
          this->m_live.add_use (v);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      linear_expression_t rhs () const { return m_rhs; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef assignment <Number, VariableName> assignment_t;
        return boost::static_pointer_cast< statement_t, assignment_t >
            (boost::make_shared<assignment_t>(m_lhs, m_rhs));
      }
      
      virtual void write(crab_os& o) const
      {
        o << m_lhs << " = " << m_rhs; // << " " << this->m_live;
        return;
      }
      
    }; 
    
    template<class Number, class VariableName>
    class assume_stmt: public statement <Number, VariableName>
    {
      
     public:
      
      typedef statement<Number,VariableName> statement_t;                  
      typedef ikos::variable< Number, VariableName > variable_t;
      typedef ikos::linear_constraint< Number, VariableName > linear_constraint_t;
      
     private:
      
      linear_constraint_t m_cst;
      
     public:
      
      assume_stmt (linear_constraint_t cst): 
	statement_t (ASSUME), m_cst(cst) 
      { 
        for(auto v: cst.variables())
          this->m_live.add_use (v); 
      }
      
      linear_constraint_t constraint() const { return m_cst; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef assume_stmt <Number, VariableName> assume_t;
        return boost::static_pointer_cast< statement_t, assume_t >
            (boost::make_shared<assume_t>(m_cst));
      }
      
      virtual void write (crab_os & o) const
      {
        o << "assume (" << m_cst << ")"; //  << " " << this->m_live;
        return;
      }
    }; 

    template< class Number, class VariableName>
    class unreachable_stmt: public statement<Number, VariableName> 
    {
     public:

      typedef statement<Number,VariableName> statement_t;
      
      unreachable_stmt(): statement_t(UNREACH) { }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef unreachable_stmt <Number, VariableName> unreachable_t;
        return boost::static_pointer_cast< statement_t, unreachable_t >
            (boost::make_shared<unreachable_t>());
      }
      
      virtual void write(crab_os& o) const
      {
        o << "unreachable";
        return;
      }
      
    }; 
  
    template<class Number, class VariableName>
    class havoc_stmt: public statement<Number, VariableName> 
    {

     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;

     private:
      
      variable_t m_lhs;

     public:
      
      havoc_stmt (variable_t lhs): statement_t(HAVOC), m_lhs(lhs)  {
        this->m_live.add_def (m_lhs);
      }
      
      variable_t variable () const { return m_lhs; }
      
      virtual void accept (statement_visitor<Number, VariableName> *v) 
      {
        v->visit (*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef havoc_stmt <Number, VariableName> havoc_t;
        return boost::static_pointer_cast< statement_t, havoc_t >
            (boost::make_shared<havoc_t>(m_lhs));
      }
      
      void write (crab_os& o) const
      {
        o << m_lhs << " =*" << " "; // << this->m_live;
        return;
      }
    }; 

    // select x, c, e1, e2:
    //    if c > 0 then x=e1 else x=e2
    //
    // Note that a select instruction is not strictly needed and can be
    // simulated by splitting blocks. However, frontends like LLVM can
    // generate many select instructions so we prefer to support
    // natively to avoid a blow up in the size of the CFG.
    template< class Number, class VariableName>
    class select_stmt: public statement <Number, VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;      
      typedef ikos::variable< Number, VariableName > variable_t;
      typedef ikos::linear_expression< Number, VariableName > linear_expression_t;
      typedef ikos::linear_constraint< Number, VariableName > linear_constraint_t;
      
     private:
      
      variable_t          m_lhs;
      linear_constraint_t m_cond;
      linear_expression_t m_e1;
      linear_expression_t m_e2;
      
     public:
      
      select_stmt (variable_t lhs, 
		   linear_constraint_t cond, 
		   linear_expression_t e1, 
		   linear_expression_t e2): 
          statement_t(SELECT),
          m_lhs(lhs), m_cond(cond), m_e1(e1), m_e2(e2) 
      { 
        this->m_live.add_def (m_lhs);
        for (auto v: m_cond.variables())
          this->m_live.add_use (v); 
        for (auto v: m_e1.variables())
          this->m_live.add_use (v); 
        for (auto v: m_e2.variables())
          this->m_live.add_use (v);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      linear_constraint_t cond () const { return m_cond; }
      
      linear_expression_t left () const { return m_e1; }
      
      linear_expression_t right () const { return m_e2; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef select_stmt <Number, VariableName> select_t;
        return boost::static_pointer_cast< statement_t, select_t >
            (boost::make_shared<select_t>(m_lhs, m_cond, m_e1, m_e2));
      }
      
      virtual void write (crab_os& o) const
      {
        o << m_lhs << " = " 
          << "ite(" << m_cond << "," << m_e1 << "," << m_e2 << ")";
        return;
      }
    }; 

    template<class Number, class VariableName>
    class assert_stmt: public statement <Number, VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;            
      typedef ikos::variable< Number, VariableName > variable_t;
      typedef ikos::linear_constraint< Number, VariableName > linear_constraint_t;
      
     private:
      
      linear_constraint_t m_cst;

     public:
      
      assert_stmt (linear_constraint_t cst, debug_info dbg_info = debug_info ())
          : statement_t (ASSERT, dbg_info), 
            m_cst(cst)
      { 
        for(auto v: cst.variables())
          this->m_live.add_use (v); 
      }
      
      linear_constraint_t constraint() const { return m_cst; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef assert_stmt <Number, VariableName> assert_t;
        return boost::static_pointer_cast< statement_t, assert_t >
	  (boost::make_shared<assert_t>(m_cst, this->m_dbg_info));
      }
      
      virtual void write (crab_os & o) const
      {
        o << "assert (" << m_cst << ")"; 
        return;
      }
    }; 

    template<class Number, class VariableName>
    class int_cast_stmt: public statement<Number,VariableName> {
     public:

      typedef ikos::variable<Number, VariableName> variable_t;
      typedef statement<Number,VariableName> statement_t;      
      typedef typename variable_t::bitwidth_t bitwidth_t;
      
     private:
      
      cast_operation_t  m_op;
      variable_t m_src;
      variable_t m_dst;

     public:
      
      int_cast_stmt (cast_operation_t op,
		     variable_t src, variable_t dst, 
		     debug_info dbg_info = debug_info ())
  	: statement_t (INT_CAST, dbg_info),
	  m_op(op), m_src(src), m_dst(dst) { 
        this->m_live.add_use (m_src);
        this->m_live.add_def (m_dst);	
      }
      
      cast_operation_t op() const {return m_op;}
      variable_t src() const {return m_src;}
      bitwidth_t src_width() const {return m_src.get_bitwidth();}
      variable_t dst() const {return m_dst;}
      bitwidth_t dst_width() const {return m_dst.get_bitwidth();}      
      
      virtual void accept(statement_visitor<Number,VariableName> *v) {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const {
        typedef int_cast_stmt<Number, VariableName> int_cast_t;
        return boost::static_pointer_cast<statement_t, int_cast_t>
	  (boost::make_shared<int_cast_t>(m_op, m_src, m_dst, this->m_dbg_info));
      }
      
      virtual void write (crab_os& o) const {
	// bitwidths are casted to int, otherwise operator<< may try
	// to print them as characters if bitwidth_t = uint8_t
	o << m_op << " " 
	  << m_src << ":" << (int) src_width() << " to " << m_dst << ":" << (int) dst_width();
        return;
      }
    }; 

    
    /*
      Array statements 
    */
  
    // XXX: the array statements array_assume_stmt, array_store_stmt,
    // and array_load_stmt take as one of their template parameters
    // Number which is the type for the array indexes. Although array
    // indexes should be always integers we keep it as a template
    // parameter in case an analysis over a type different from
    // integers (e.g., reals) is done. Note that we don't allow mixing
    // non-integers and integers so we cannot have analysis where all
    // variables are non-integers except array indexes.
    
    
    //! Assume all the array elements are equal to some variable or
    //! number.
    template<class Number, class VariableName>
    class array_assume_stmt: public statement<Number, VariableName> 
    {
     public:

      typedef statement<Number,VariableName> statement_t;                  
      typedef ikos::linear_expression<Number, VariableName> linear_expression_t;
      typedef ikos::variable<Number, VariableName> variable_t;
      typedef typename variable_t::type_t type_t;
      
     private:

      // forall i \in [lb,ub] modulo elem_size. arr[i] == val
      variable_t m_arr; 
      uint64_t m_elem_size;
      linear_expression_t m_lb;
      linear_expression_t m_ub;
      linear_expression_t m_val;

      inline bool is_number_or_variable (linear_expression_t e)  const {
	return (e.is_constant() || e.get_variable());
      }
      
     public:
      
      array_assume_stmt (variable_t arr, uint64_t elem_size,
                         linear_expression_t lb, linear_expression_t ub,
			 linear_expression_t val): 
          statement_t (ARR_ASSUME),
          m_arr (arr),  m_elem_size (elem_size),
	  m_lb (lb) , m_ub (ub), m_val (val)  {
	
	if (!is_number_or_variable (m_lb))
	  CRAB_ERROR("array_assume third parameter can only be number or variable");
	
	if (!is_number_or_variable (m_ub))
	  CRAB_ERROR("array_assume forth parameter can only be number or variable");

	if (!is_number_or_variable (m_val))
	  CRAB_ERROR("array_assume fifth parameter can only be number or variable");

        this->m_live.add_use (m_arr);
        for(auto v: m_lb.variables()) 
          this->m_live.add_use (v);
        for(auto v: m_ub.variables()) 
          this->m_live.add_use (v);
	for(auto v: m_val.variables())
	  this->m_live.add_use (v);
      }
      
      variable_t array () const { return m_arr; }
      
      type_t array_type () const { return m_arr.get_type(); }

      uint64_t elem_size () const { return m_elem_size;}
       
      linear_expression_t lb_index () const { return m_lb;}
      
      linear_expression_t ub_index () const { return m_ub;}

      linear_expression_t val () const { return m_val;}
      
      virtual void accept (statement_visitor<Number, VariableName> *v) {
        v->visit (*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef array_assume_stmt <Number, VariableName> array_assume_t;
        return boost::static_pointer_cast< statement_t, array_assume_t >
	  (boost::make_shared<array_assume_t>(m_arr, m_elem_size,
					      m_lb, m_ub, m_val));
      }
      
      void write (crab_os& o) const
      {
        o << "assume (forall l in [" << m_lb << "," << m_ub << "] % " << m_elem_size
	  << " :: " 
          << m_arr << "[l]=" << m_val << ")";          
        return;
      }
    }; 
    
    template<class Number, class VariableName>
    class array_store_stmt: public statement<Number, VariableName>
    {
     public:

      typedef statement<Number,VariableName> statement_t;                  
      typedef ikos::linear_expression<Number, VariableName> linear_expression_t;
      typedef ikos::variable<Number, VariableName> variable_t;
      typedef typename variable_t::type_t type_t;
      
     private:
      
      variable_t m_arr;
      linear_expression_t m_index;
      linear_expression_t m_value;
      uint64_t m_elem_size; //! size in bytes
      bool m_is_singleton; //! whether the store writes to a singleton
                           //  cell. If unknown set to false.

      inline bool is_number_or_variable (linear_expression_t e)  const {
	return (e.is_constant() || e.get_variable());
      }
      
     public:
      
      array_store_stmt (variable_t arr, 
                        linear_expression_t index,
			linear_expression_t value,  
                        uint64_t elem_size, bool is_sing = false)
          : statement_t(ARR_STORE),
            m_arr (arr), m_index (index), 
            m_value (value), m_elem_size (elem_size),  
            m_is_singleton (is_sing) {
	
        if (array_type() < ARR_BOOL_TYPE)
          CRAB_ERROR ("array_store must have array type");

	if (!is_number_or_variable (m_value))
	  CRAB_ERROR ("array_store forth parameter only number or variable");
	
        this->m_live.add_use (m_arr);
        for(auto v: m_index.variables()) 
          this->m_live.add_use (v);
	for(auto v: m_value.variables())
	  this->m_live.add_use (v);
      }
      
      variable_t array () const { return m_arr; }
      
      linear_expression_t index () const { return m_index; }
      
      linear_expression_t value () const { return m_value; }
      
      type_t array_type () const { return m_arr.get_type(); }

      uint64_t elem_size () const { return m_elem_size; }
      
      bool is_singleton () const { return m_is_singleton;}
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef array_store_stmt <Number, VariableName> array_store_t;
        return boost::static_pointer_cast<statement_t, array_store_t>
            (boost::make_shared<array_store_t>(m_arr, m_index,
					       m_value, m_elem_size, m_is_singleton)); 
      }
      
      virtual void write(crab_os& o) const
      {
        o << "array_store(" 
          << m_arr << "," << m_index << "," << m_value 
          << ")"; 
        return;
      }
    }; 

    template<class Number, class VariableName>
    class array_load_stmt: public statement<Number, VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;                        
      typedef ikos::linear_expression<Number, VariableName > linear_expression_t;
      typedef ikos::variable<Number, VariableName> variable_t;
      typedef typename variable_t::type_t type_t;
      
     private:

      variable_t m_lhs;
      variable_t m_array;
      linear_expression_t m_index;
      uint64_t m_elem_size; //! size in bytes
      
     public:
      
      array_load_stmt (variable_t lhs, variable_t arr, 
		       linear_expression_t index, uint64_t elem_size) 
	: statement_t (ARR_LOAD),
	  m_lhs (lhs), m_array (arr), 
	  m_index (index), m_elem_size (elem_size) {
        if (array_type() < ARR_BOOL_TYPE)
          CRAB_ERROR ("array_load must have array type");
          
        this->m_live.add_def (lhs);
        this->m_live.add_use (m_array);
        for(auto v: m_index.variables()) 
          this->m_live.add_use (v);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      variable_t array () const { return m_array; }

      type_t array_type () const { return m_array.get_type(); }
      
      linear_expression_t index () const { return m_index; }
      
      uint64_t elem_size () const { return m_elem_size; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef array_load_stmt <Number, VariableName> array_load_t;
        return boost::static_pointer_cast< statement_t, array_load_t>
            (boost::make_shared<array_load_t>(m_lhs, m_array,
					      m_index, m_elem_size));
                                              
      }
      
      virtual void write(crab_os& o) const {
        o << m_lhs << " = " 
          << "array_load(" << m_array << "," << m_index  << ")"; 
        return;
      }
    }; 

    template<class Number, class VariableName>
    class array_assign_stmt: public statement<Number, VariableName>
    {
      //! a = b


     public:
      
      typedef statement<Number,VariableName> statement_t;                              
      typedef ikos::variable<Number, VariableName> variable_t;
      typedef typename variable_t::type_t type_t;
      
     private:
      
      variable_t m_lhs;
      variable_t m_rhs;

     public:
      
      array_assign_stmt (variable_t lhs, variable_t rhs)
	: statement_t (ARR_ASSIGN),
	  m_lhs (lhs), m_rhs (rhs) {
        if (array_type() < ARR_BOOL_TYPE || m_lhs.get_type() != m_rhs.get_type())
          CRAB_ERROR ("array_assign must have array type");
	
        this->m_live.add_def (lhs);
        this->m_live.add_use (rhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      variable_t rhs () const { return m_rhs; }
      
      type_t array_type () const { return m_lhs.get_type(); }

      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef array_assign_stmt <Number, VariableName> arr_assign_t;
        return boost::static_pointer_cast< statement_t, arr_assign_t >
            (boost::make_shared<arr_assign_t>(m_lhs, m_rhs));
      }
      
      virtual void write(crab_os& o) const
      {
        o << m_lhs << " = "  << m_rhs;
        return;
      }
    }; 
  
  
    /*
      Pointer statements (PTR_TYPE)
    */

    template<class Number, class VariableName>
    class ptr_load_stmt: public statement<Number, VariableName>
    {
      // p = *q
      
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number, VariableName> variable_t;

     private:

      variable_t m_lhs;
      variable_t m_rhs;

     public:

      ptr_load_stmt (variable_t lhs, variable_t rhs, debug_info dbg_info = debug_info ())
	: statement_t (PTR_LOAD, dbg_info), m_lhs (lhs), m_rhs (rhs) {
        this->m_live.add_use (lhs);
        this->m_live.add_use (rhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      variable_t rhs () const { return m_rhs; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t > clone () const
      {
        typedef ptr_load_stmt <Number, VariableName> ptr_load_t;
        return boost::static_pointer_cast< statement_t, ptr_load_t >
	  (boost::make_shared<ptr_load_t>(m_lhs, m_rhs, this->m_dbg_info));
      }
      
      virtual void write(crab_os& o) const
      {
        o << m_lhs << " = "  << "*(" << m_rhs << ")";
        return;
      }
      
    }; 

    template<class Number, class VariableName>
    class ptr_store_stmt: public statement<Number, VariableName>
    {
      // *p = q

     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number, VariableName> variable_t;

     private:
      
      variable_t m_lhs;
      variable_t m_rhs;
      
     public:
      
      ptr_store_stmt (variable_t lhs, variable_t rhs,
		      debug_info dbg_info = debug_info ())
          : statement_t (PTR_STORE, dbg_info), m_lhs (lhs), m_rhs (rhs) {
        this->m_live.add_use (lhs);
        this->m_live.add_use (rhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      variable_t rhs () const { return m_rhs; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef ptr_store_stmt <Number, VariableName> ptr_store_t;
        return boost::static_pointer_cast< statement_t, ptr_store_t >
            (boost::make_shared<ptr_store_t>(m_lhs, m_rhs));
      }
      
      virtual void write(crab_os& o) const
      {
        o << "*(" << m_lhs << ") = " << m_rhs;
        return;
      }
    }; 

    template<class Number, class VariableName>
    class ptr_assign_stmt: public statement<Number, VariableName>
    {
      //! p = q + n
     public:

      typedef statement<Number,VariableName> statement_t;                                    
      typedef ikos::variable<Number, VariableName > variable_t;
      typedef ikos::linear_expression<Number, VariableName > linear_expression_t;
      
     private:
      
      variable_t m_lhs;
      variable_t m_rhs;
      linear_expression_t m_offset;
      
     public:
      
      ptr_assign_stmt (variable_t lhs, variable_t rhs, linear_expression_t offset)
	: statement_t (PTR_ASSIGN), m_lhs (lhs), m_rhs (rhs), m_offset(offset) {
        this->m_live.add_def (lhs);
        this->m_live.add_use (rhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      variable_t rhs () const { return m_rhs; }
      
      linear_expression_t offset () const { return m_offset; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef ptr_assign_stmt <Number, VariableName> ptr_assign_t;
        return boost::static_pointer_cast< statement_t, ptr_assign_t >
            (boost::make_shared<ptr_assign_t>(m_lhs, m_rhs, m_offset));
      }
      
      virtual void write(crab_os& o) const
      {
        o << m_lhs << " = "  << "&(" << m_rhs << ") + ";
        linear_expression_t off (m_offset) ; // FIX: write method is not const in ikos 
        off.write (o);
        return;
      }
    }; 
  
    template<class Number, class VariableName>
    class ptr_object_stmt: public statement<Number, VariableName>
    {
      //! lhs = &a;

     public:
      
      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number, VariableName> variable_t;

     private:
      
      variable_t m_lhs;
      ikos::index_t m_address;
      
     public:
      
      ptr_object_stmt (variable_t lhs, ikos::index_t address)
	: statement_t (PTR_OBJECT), m_lhs (lhs), m_address (address) {
        this->m_live.add_def(lhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      ikos::index_t rhs () const { return m_address; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef ptr_object_stmt <Number, VariableName> ptr_object_t;
        return boost::static_pointer_cast< statement_t, ptr_object_t>
            (boost::make_shared<ptr_object_t>(m_lhs, m_address));
      }
      
      virtual void write(crab_os& o) const
      {
        o << m_lhs << " = "  << "&(" << m_address << ")" ;
        return;
      }
    }; 

    template<class Number, class VariableName>
    class ptr_function_stmt: public statement<Number, VariableName>
    {
      // lhs = &func;
      
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number, VariableName> variable_t;

     private:
      
      variable_t m_lhs;
      VariableName m_func; // Pre: function names are unique 
      
     public:
      
      ptr_function_stmt (variable_t lhs, VariableName func)
	: statement_t (PTR_FUNCTION), m_lhs (lhs), m_func (func) {
        this->m_live.add_def(lhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      VariableName rhs () const { return m_func; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef ptr_function_stmt <Number, VariableName> ptr_function_t;
        return boost::static_pointer_cast< statement_t, ptr_function_t>
            (boost::make_shared<ptr_function_t>(lhs (), rhs ()));
      }
      
      virtual void write(crab_os& o) const
      {
        o << m_lhs << " = "  << "&(" << m_func << ")" ;
        return;
      }
    }; 

    template<class Number, class VariableName>
    class ptr_null_stmt: public statement<Number, VariableName>
    {
      //! lhs := null;
      
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number, VariableName> variable_t;

     private:
      
      variable_t m_lhs;

     public:
      
      ptr_null_stmt (variable_t lhs)
	: statement_t (PTR_NULL), m_lhs (lhs) {
        this->m_live.add_def(m_lhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef ptr_null_stmt <Number, VariableName> ptr_null_t;
        return boost::static_pointer_cast<statement_t, ptr_null_t>
            (boost::make_shared<ptr_null_t>(m_lhs));
      }
      
      virtual void write(crab_os& o) const
      {
        o << m_lhs << " = "  << "NULL";
        return;
      }
    }; 

    template<class Number, class VariableName>
    class ptr_assume_stmt: public statement<Number, VariableName>
    {
     public:

      
      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;
      typedef pointer_constraint<variable_t> ptr_cst_t;

     private:

      ptr_cst_t m_cst;
      
     public:
      
      ptr_assume_stmt (ptr_cst_t cst)
	: statement_t (PTR_ASSUME),
	  m_cst (cst) { 
        if (!cst.is_tautology () && !cst.is_contradiction ()) {
          if (cst.is_unary ()) {
            this->m_live.add_use (cst.lhs());
          } else {
            this->m_live.add_use (cst.lhs());
            this->m_live.add_use (cst.rhs());
          }
        }
      }
      
      ptr_cst_t constraint () const { return m_cst; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef ptr_assume_stmt <Number, VariableName> ptr_assume_t;
        return boost::static_pointer_cast<statement_t, ptr_assume_t>
            (boost::make_shared<ptr_assume_t>(m_cst));
      }
      
      virtual void write(crab_os& o) const
      {
        o << "assume_ptr(" << m_cst << ")";
        return;
      }
    }; 

    template<class Number, class VariableName>
    class ptr_assert_stmt: public statement<Number, VariableName>
    {
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;
      typedef pointer_constraint<variable_t> ptr_cst_t;

     private:

      ptr_cst_t m_cst;
      
     public:
      
      ptr_assert_stmt (ptr_cst_t cst, debug_info dbg_info = debug_info ())
          : statement_t(PTR_ASSERT, dbg_info),
            m_cst (cst) { 
        if (!cst.is_tautology () && !cst.is_contradiction ()) {
          if (cst.is_unary ()) {
            this->m_live.add_use (cst.lhs());
          } else {
            this->m_live.add_use (cst.lhs());
            this->m_live.add_use (cst.rhs());
          }
        }
      }
      
      ptr_cst_t constraint () const { return m_cst; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef ptr_assert_stmt <Number, VariableName> ptr_assert_t;
        return boost::static_pointer_cast< statement_t, ptr_assert_t>
	  (boost::make_shared<ptr_assert_t>(m_cst, this->m_dbg_info));
      }
      
      virtual void write(crab_os& o) const
      {
        o << "assert_ptr(" << m_cst << ")";
        return;
      }
    }; 
  
  
    /*
      Function calls
    */
  
    template<class Number, class VariableName>
    class callsite_stmt: public statement<Number, VariableName>
    {
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;
      typedef typename variable_t::type_t type_t;
      
     private:

      std::string m_func_name;      
      std::vector<variable_t> m_lhs;
      std::vector<variable_t> m_args;
      
      typedef typename std::vector<variable_t>::iterator iterator;
      typedef typename std::vector<variable_t>::const_iterator const_iterator;
      
     public:
      
      callsite_stmt (std::string func_name, const std::vector<variable_t> &args)
	: statement_t (CALLSITE), m_func_name (func_name) {
	
        std::copy (args.begin (), args.end (), std::back_inserter (m_args));
        for (auto arg:  m_args) { this->m_live.add_use (arg); }
      }
      
      callsite_stmt (std::string func_name,
		     const std::vector<variable_t> &lhs, 
		     const std::vector<variable_t> &args)
	: statement_t (CALLSITE), m_func_name (func_name) {
	
        std::copy (args.begin (), args.end (), std::back_inserter(m_args));
        for (auto arg:  m_args) { this->m_live.add_use (arg); }

        std::copy (lhs.begin (), lhs.end (), std::back_inserter(m_lhs));
        for (auto arg:  m_lhs) { this->m_live.add_def (arg); }
      }
      
      const std::vector<variable_t>& get_lhs () const { 
        return m_lhs;
      }
      
      std::string get_func_name () const { 
        return m_func_name; 
      }

      const std::vector<variable_t>& get_args () const { 
        return m_args;
      }

      unsigned get_num_args () const { return m_args.size (); }

      variable_t get_arg_name (unsigned idx) const { 
        if (idx >= m_args.size ())
          CRAB_ERROR ("Out-of-bound access to call site parameter");
        
        return m_args[idx];
      }
      
      type_t get_arg_type (unsigned idx) const { 
        if (idx >= m_args.size ())
        CRAB_ERROR ("Out-of-bound access to call site parameter");
        
        return m_args[idx].get_type();
      }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) {
        v->visit(*this);
      }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef callsite_stmt <Number, VariableName> call_site_t;
        return boost::static_pointer_cast< statement_t, call_site_t>
            (boost::make_shared<call_site_t> (m_func_name, m_lhs, m_args));
      }

      virtual void write(crab_os& o) const
      {
        if (m_lhs.empty ()) {
          // do nothing
        } else if (m_lhs.size () == 1) {
          o << (*m_lhs.begin()) << " =";
        } else {
          o << "(";
          for (const_iterator It = m_lhs.begin (), Et=m_lhs.end (); It!=Et; )
          {
            o << (*It);
            ++It;
            if (It != Et) o << ",";
          }
          o << ")=";
        } 
        o << " call " << m_func_name << "(";
        for (const_iterator It = m_args.begin (), Et=m_args.end (); It!=Et; )
        {
          o << *It << ":" << (*It).get_type();
          ++It;
          if (It != Et)
            o << ",";
        }
        o << ")";
        return;
      }
      
    }; 
  
    template<class Number, class VariableName>
    class return_stmt: public statement<Number, VariableName>
    {

     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;
      typedef typename variable_t::type_t type_t;

     private:

      std::vector<variable_t> m_ret;
      
     public:
      
      return_stmt (variable_t var)
          : statement_t (RETURN)
      {
        m_ret.push_back (var);
        this->m_live.add_use (var); 
      }

      return_stmt (const std::vector<variable_t> &ret_vals)
          : statement_t (RETURN)
      {
        std::copy (ret_vals.begin (), ret_vals.end (), std::back_inserter(m_ret));
        for (auto r:  m_ret) { this->m_live.add_use (r); }
      }
      
      const std::vector<variable_t>& get_ret_vals () const {
        return m_ret;
      }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      {
        v->visit(*this);
      }

      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef return_stmt <Number, VariableName> return_t;
        return boost::static_pointer_cast<statement_t, return_t>
            (boost::make_shared<return_t>(m_ret));
      }
      
      virtual void write(crab_os& o) const
      {
        o << "return ";
        
        if (m_ret.size () == 1) {
          o << (*m_ret.begin());
        } else if (m_ret.size () > 1) {
          o << "(";
          for (auto It = m_ret.begin (), Et=m_ret.end (); It!=Et; ) {
            o << (*It);
            ++It;
            if (It != Et) o << ",";
          }
          o << ")";
        }
        return;
      }
    }; 

    /* 
       Boolean statements
    */

    template<class Number, class VariableName>
    class bool_assign_cst: public statement<Number, VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;
      typedef ikos::linear_constraint<Number,VariableName> linear_constraint_t;
      
     private:
      
      variable_t          m_lhs; // pre: BOOL_TYPE
      linear_constraint_t m_rhs;
      
     public:
      
      bool_assign_cst (variable_t lhs, linear_constraint_t rhs)
	: statement_t (BOOL_ASSIGN_CST), m_lhs(lhs), m_rhs(rhs) 
      {
        this->m_live.add_def (m_lhs);
        for(auto v: m_rhs.variables()) 
          this->m_live.add_use (v);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      linear_constraint_t rhs () const { return m_rhs; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      { v->visit(*this); }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef bool_assign_cst <Number, VariableName> bool_assign_cst_t;
        return boost::static_pointer_cast< statement_t, bool_assign_cst_t >
	  (boost::make_shared<bool_assign_cst_t>(m_lhs, m_rhs));
      }
      
      virtual void write(crab_os& o) const
      {
	if (m_rhs.is_tautology ())
	  o << m_lhs << " = true ";
	else if (m_rhs.is_contradiction ())
	  o << m_lhs << " = false ";
	else 
	  o << m_lhs << " = (" << m_rhs << ")";
      }
    }; 

    template<class Number, class VariableName>
    class bool_assign_var: public statement<Number, VariableName>
    {
      // Note that this can be simulated with bool_binary_op (e.g.,
      // b1 := b2 ----> b1 := b2 or false). However, we create a
      // special statement to assign a variable to another because it
      // is a very common operation.
     public:

      typedef statement<Number,VariableName> statement_t;            
      typedef ikos::variable<Number,VariableName> variable_t;
      
     private:
      
      variable_t m_lhs; // pre: BOOL_TYPE
      variable_t m_rhs; // pre: BOOL_TYPE
      
      // if m_is_rhs_negated is true then lhs := not(rhs).
      bool m_is_rhs_negated;
      
     public:
      
      bool_assign_var (variable_t lhs, variable_t rhs, bool is_not_rhs)
	: statement_t (BOOL_ASSIGN_VAR),
	  m_lhs(lhs), m_rhs(rhs), m_is_rhs_negated (is_not_rhs) {
        this->m_live.add_def (m_lhs);
	this->m_live.add_use (m_rhs);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      variable_t rhs () const { return m_rhs; }

      bool is_rhs_negated () const {
	return m_is_rhs_negated;
      }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      { v->visit(*this); }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef bool_assign_var <Number, VariableName> bool_assign_var_t;
        return boost::static_pointer_cast< statement_t, bool_assign_var_t >
	  (boost::make_shared<bool_assign_var_t>(m_lhs, m_rhs, m_is_rhs_negated));
      }
      
      virtual void write(crab_os& o) const {
	o << m_lhs << " = "	;
	if (is_rhs_negated ())
	  o << "not(" << m_rhs << ")";
	else
	  o <<  m_rhs;	  
      }
    }; 
    
    template< class Number, class VariableName>
    class bool_binary_op: public statement <Number,VariableName>
    {
      // b1:= b2 and b3
      // b1:= b2 or b3
      // b1:= b2 xor b3
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;
      
     private:
      
      variable_t m_lhs; // pre: BOOL_TYPE
      bool_binary_operation_t m_op; 
      variable_t m_op1; // pre: BOOL_TYPE
      variable_t m_op2; // pre: BOOL_TYPE
      
     public:
      
      bool_binary_op (variable_t lhs, 
		      bool_binary_operation_t op, variable_t op1, variable_t op2,
		      debug_info dbg_info = debug_info ())
  	: statement_t (BOOL_BIN_OP, dbg_info),
	  m_lhs(lhs), m_op(op), m_op1(op1), m_op2(op2) 
      { 
        this->m_live.add_def (m_lhs);
        this->m_live.add_use (m_op1);
        this->m_live.add_use (m_op2);	
      }
      
      variable_t lhs () const { return m_lhs; }
      
      bool_binary_operation_t op () const { return m_op; }
      
      variable_t left () const { return m_op1; }
      
      variable_t right () const { return m_op2; }
      
      virtual void accept(statement_visitor <Number,VariableName> *v) 
      { v->visit(*this); }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef bool_binary_op<Number, VariableName> bool_binary_op_t;
        return boost::static_pointer_cast<statement_t, bool_binary_op_t>
	  (boost::make_shared<bool_binary_op_t>(m_lhs, m_op, m_op1, m_op2,
						this->m_dbg_info));
      }
      
      virtual void write (crab_os& o) const
      { o << m_lhs << " = " << m_op1 << m_op << m_op2; }
    }; 
    
    template<class Number, class VariableName>
    class bool_assume_stmt: public statement <Number, VariableName>
    {
      
     public:
      
      typedef statement<Number,VariableName> statement_t;                  
      typedef ikos::variable<Number,VariableName> variable_t;
      
     private:
      
      variable_t m_var;  // pre: BOOL_TYPE
      bool m_is_negated;
      
     public:
      
      bool_assume_stmt (variable_t v, bool is_negated):
	statement_t(BOOL_ASSUME), m_var(v), m_is_negated (is_negated) 
      { this->m_live.add_use (v); }
      
      variable_t cond() const { return m_var; }

      bool is_negated () const { return m_is_negated;}
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      { v->visit(*this); }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef bool_assume_stmt <Number, VariableName> bool_assume_t;
        return boost::static_pointer_cast< statement_t, bool_assume_t >
	  (boost::make_shared<bool_assume_t>(m_var, m_is_negated));
      }
      
      virtual void write (crab_os & o) const
      {
	if (m_is_negated)
	  o << "assume (not(" << m_var << "))";
	else
	  o << "assume (" << m_var << ")";	
      }
    }; 

    // select b1, b2, b3, b4:
    //    if b2 then b1=b3 else b1=b4
    template< class Number, class VariableName>
    class bool_select_stmt: public statement <Number, VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;      
      typedef ikos::variable<Number,VariableName> variable_t;
      
     private:
      
      variable_t m_lhs;  // pre: BOOL_TYPE
      variable_t m_cond; // pre: BOOL_TYPE
      variable_t m_b1;   // pre: BOOL_TYPE
      variable_t m_b2;   // pre: BOOL_TYPE
      
     public:
      
      bool_select_stmt (variable_t lhs, variable_t cond, variable_t b1, variable_t b2)
	: statement_t(BOOL_SELECT), m_lhs(lhs), m_cond(cond), m_b1(b1), m_b2(b2) 
      { 
        this->m_live.add_def (m_lhs);
	this->m_live.add_use (m_cond); 
	this->m_live.add_use (m_b1); 
	this->m_live.add_use (m_b2);
      }
      
      variable_t lhs () const { return m_lhs; }
      
      variable_t cond () const { return m_cond; }
      
      variable_t left () const { return m_b1; }
      
      variable_t right () const { return m_b2; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      { v->visit(*this); }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef bool_select_stmt <Number, VariableName> bool_select_t;
        return boost::static_pointer_cast< statement_t, bool_select_t >
            (boost::make_shared<bool_select_t>(m_lhs, m_cond, m_b1, m_b2));
      }
      
      virtual void write (crab_os& o) const
      {
        o << m_lhs << " = " 
          << "ite(" << m_cond << "," << m_b1 << "," << m_b2 << ")";
      }
    }; 

    template<class Number, class VariableName>
    class bool_assert_stmt: public statement <Number, VariableName>
    {
      
     public:

      typedef statement<Number,VariableName> statement_t;
      typedef ikos::variable<Number,VariableName> variable_t;
            
     private:
      
      variable_t m_var;  // pre: BOOL_TYPE

     public:
      
      bool_assert_stmt (variable_t v, debug_info dbg_info = debug_info ())
	: statement_t (BOOL_ASSERT, dbg_info), m_var(v)
      { this->m_live.add_use (v); }
      
      variable_t cond() const { return m_var; }
      
      virtual void accept(statement_visitor <Number, VariableName> *v) 
      { v->visit(*this); }
      
      virtual boost::shared_ptr<statement_t> clone () const
      {
        typedef bool_assert_stmt <Number, VariableName> bool_assert_t;
        return boost::static_pointer_cast< statement_t, bool_assert_t >
	  (boost::make_shared<bool_assert_t>(m_var, this->m_dbg_info));
      }
      
      virtual void write (crab_os & o) const
      { o << "assert (" << m_var << ")"; }
    }; 
    
    template< class BasicBlockLabel, class VariableName, class Number>
    class Cfg;
  
    template< class BasicBlockLabel, class VariableName, class Number>
    class basic_block: public boost::noncopyable
    {
      
      // TODO: support for removing statements 
      friend class Cfg< BasicBlockLabel, VariableName, Number>;
      
     public:

      typedef Number number_t;
      typedef VariableName varname_t;
      typedef BasicBlockLabel basic_block_label_t;

      // helper types to build statements
      typedef ikos::variable< Number, VariableName > variable_t;
      typedef ikos::linear_expression< Number, VariableName > lin_exp_t;
      typedef ikos::linear_constraint< Number, VariableName > lin_cst_t;
      typedef statement< Number, VariableName> statement_t;
      typedef basic_block< BasicBlockLabel, VariableName, Number> basic_block_t;
      typedef ikos::interval <Number> interval_t;
      
     private:
      
      typedef std::vector< BasicBlockLabel > bb_id_set_t;
      typedef boost::shared_ptr< statement_t > statement_ptr;
      typedef std::vector< statement_ptr > stmt_list_t;
      
     public:
      
      typedef typename bb_id_set_t::iterator succ_iterator;
      typedef typename bb_id_set_t::const_iterator const_succ_iterator;
      typedef succ_iterator pred_iterator;
      typedef const_succ_iterator const_pred_iterator;
      typedef boost::indirect_iterator< typename stmt_list_t::iterator > iterator;
      typedef boost::indirect_iterator< typename stmt_list_t::const_iterator > const_iterator;
      typedef boost::indirect_iterator< typename stmt_list_t::reverse_iterator > reverse_iterator;
      typedef boost::indirect_iterator< typename stmt_list_t::const_reverse_iterator > const_reverse_iterator;
      typedef ikos::discrete_domain<variable_t> live_domain_t;
      
     public:

      typedef havoc_stmt<Number, VariableName> havoc_t;
      typedef unreachable_stmt<Number, VariableName> unreach_t;
      // Numerical
      typedef binary_op<Number,VariableName>   bin_op_t;
      typedef assignment<Number,VariableName>  assign_t;
      typedef assume_stmt<Number,VariableName> assume_t;
      typedef select_stmt<Number,VariableName> select_t;
      typedef assert_stmt<Number,VariableName> assert_t;
      typedef int_cast_stmt<Number, VariableName> int_cast_t;
      // Functions
      typedef callsite_stmt<Number, VariableName> callsite_t;
      typedef return_stmt<Number, VariableName> return_t;
      // Arrays
      typedef array_assume_stmt<Number,VariableName>  arr_assume_t; 
      typedef array_store_stmt<Number,VariableName>   arr_store_t;
      typedef array_load_stmt<Number,VariableName>    arr_load_t;
      typedef array_assign_stmt<Number, VariableName> arr_assign_t;
      // Pointers
      typedef ptr_store_stmt<Number, VariableName>    ptr_store_t;
      typedef ptr_load_stmt<Number, VariableName>     ptr_load_t;
      typedef ptr_assign_stmt<Number,VariableName>    ptr_assign_t;
      typedef ptr_object_stmt<Number, VariableName>   ptr_object_t;
      typedef ptr_function_stmt<Number, VariableName> ptr_function_t;
      typedef ptr_null_stmt<Number, VariableName>     ptr_null_t;
      typedef ptr_assume_stmt<Number, VariableName>   ptr_assume_t;
      typedef ptr_assert_stmt<Number, VariableName>   ptr_assert_t;
      // Boolean
      typedef bool_binary_op<Number,VariableName>     bool_bin_op_t;
      typedef bool_assign_cst<Number,VariableName>    bool_assign_cst_t;
      typedef bool_assign_var<Number,VariableName>    bool_assign_var_t;      
      typedef bool_assume_stmt<Number,VariableName>   bool_assume_t;
      typedef bool_select_stmt<Number,VariableName>   bool_select_t;
      typedef bool_assert_stmt<Number,VariableName>   bool_assert_t;

     private:

      typedef boost::shared_ptr<havoc_t> havoc_ptr;
      typedef boost::shared_ptr<unreach_t> unreach_ptr;
      
      typedef boost::shared_ptr<bin_op_t> bin_op_ptr;
      typedef boost::shared_ptr<assign_t> assign_ptr;
      typedef boost::shared_ptr<assume_t> assume_ptr;
      typedef boost::shared_ptr<select_t> select_ptr;
      typedef boost::shared_ptr<assert_t> assert_ptr;
      typedef boost::shared_ptr<int_cast_t> int_cast_ptr;      
      
      typedef boost::shared_ptr<callsite_t> callsite_ptr;      
      typedef boost::shared_ptr<return_t> return_ptr;      
      typedef boost::shared_ptr<arr_assume_t> arr_assume_ptr;
      typedef boost::shared_ptr<arr_store_t> arr_store_ptr;
      typedef boost::shared_ptr<arr_load_t> arr_load_ptr;    
      typedef boost::shared_ptr<arr_assign_t> arr_assign_ptr;    
      typedef boost::shared_ptr<ptr_store_t> ptr_store_ptr;
      typedef boost::shared_ptr<ptr_load_t> ptr_load_ptr;    
      typedef boost::shared_ptr<ptr_assign_t> ptr_assign_ptr;
      typedef boost::shared_ptr<ptr_object_t> ptr_object_ptr;    
      typedef boost::shared_ptr<ptr_function_t> ptr_function_ptr;    
      typedef boost::shared_ptr<ptr_null_t> ptr_null_ptr;    
      typedef boost::shared_ptr<ptr_assume_t> ptr_assume_ptr;    
      typedef boost::shared_ptr<ptr_assert_t> ptr_assert_ptr;    

      typedef boost::shared_ptr<bool_bin_op_t> bool_bin_op_ptr;
      typedef boost::shared_ptr<bool_assign_cst_t> bool_assign_cst_ptr;
      typedef boost::shared_ptr<bool_assign_var_t> bool_assign_var_ptr;      
      typedef boost::shared_ptr<bool_assume_t> bool_assume_ptr;
      typedef boost::shared_ptr<bool_select_t> bool_select_ptr;
      typedef boost::shared_ptr<bool_assert_t> bool_assert_ptr;
      
      
      BasicBlockLabel m_bb_id;
      stmt_list_t m_stmts;
      bb_id_set_t m_prev, m_next;
      tracked_precision m_track_prec;    
      // Ideally it should be size_t to indicate any position within the
      // block. For now, we only allow to insert either at front or at
      // the back (default). Note that if insertions at the front are
      // very common we should replace stmt_list_t from a vector to a
      // deque.
      bool m_insert_point_at_front; 

      // set of used/def variables 
      live_domain_t m_live; 
      
      void insert_adjacent (bb_id_set_t &c, BasicBlockLabel e)
      { 
        if (std::find(c.begin (), c.end (), e) == c.end ())
          c.push_back (e);
      }
      
      void remove_adjacent (bb_id_set_t &c, BasicBlockLabel e)
      {
        if (std::find(c.begin (), c.end (), e) != c.end ())
          c.erase (std::remove(c.begin (), c.end (), e), c.end ());
      }
      
      basic_block (BasicBlockLabel bb_id, tracked_precision track_prec): 
          m_bb_id (bb_id), m_track_prec (track_prec), 
          m_insert_point_at_front (false), 
          m_live (live_domain_t::bottom ())
      { }
      
      static boost::shared_ptr< basic_block_t > 
      create (BasicBlockLabel bb_id, tracked_precision track_prec) 
      {
        return boost::shared_ptr<basic_block_t>(new basic_block_t(bb_id, track_prec));
      }
      
      void insert(statement_ptr stmt) 
      {
        if (m_insert_point_at_front)
        { 
          m_stmts.insert (m_stmts.begin(), stmt);
          m_insert_point_at_front = false;
        }
        else
          m_stmts.push_back(stmt);

        auto ls = stmt->get_live ();
        for (auto &v : boost::make_iterator_range (ls.uses_begin (), ls.uses_end ()))
          m_live += v;
        for (auto &v : boost::make_iterator_range (ls.defs_begin (), ls.defs_end ()))
          m_live += v;
      }
      
     public:
      
      //! it will be set to false after the first insertion
      void set_insert_point_front (){
        m_insert_point_at_front = true;
      }
      
      boost::shared_ptr<basic_block_t> clone () const {     
        boost::shared_ptr<basic_block_t> b (new basic_block_t(label (), m_track_prec));        
        for (auto &s : boost::make_iterator_range (begin (), end ()))
          b->m_stmts.push_back (s.clone ()); 
        
        for (auto id : boost::make_iterator_range (prev_blocks ())) 
          b->m_prev.push_back (id);
        
        for (auto id : boost::make_iterator_range (next_blocks ()))
          b->m_next.push_back (id);

        b->m_live = m_live;
        return b;
      }

      BasicBlockLabel label () const { return m_bb_id; }

      std::string name () const {
        return cfg_impl::get_label_str (m_bb_id); 
      }

      iterator begin() { 
        return boost::make_indirect_iterator (m_stmts.begin ()); 
      }
      iterator end() { 
        return boost::make_indirect_iterator (m_stmts.end ()); 
      }
      const_iterator begin() const { 
        return boost::make_indirect_iterator (m_stmts.begin ()); 
      }
      const_iterator end() const {
        return boost::make_indirect_iterator (m_stmts.end ()); 
      }

      reverse_iterator rbegin() {            
        return boost::make_indirect_iterator (m_stmts.rbegin ()); 
      }
      reverse_iterator rend() {              
        return boost::make_indirect_iterator (m_stmts.rend ()); 
      }
      const_reverse_iterator rbegin() const {
        return boost::make_indirect_iterator (m_stmts.rbegin ()); 
      }
      const_reverse_iterator rend() const {
        return boost::make_indirect_iterator (m_stmts.rend ()); 
      }
      
      size_t size() { return std::distance ( begin (), end ()); }

      live_domain_t& live () {
        return m_live;
      }

      live_domain_t live () const {
        return m_live;
      }

      void accept(statement_visitor<Number, VariableName> *v) {
	v->visit(*this);
      }
      
      std::pair<succ_iterator, succ_iterator> next_blocks ()
      { 
        return std::make_pair (m_next.begin (), m_next.end ());
      }
      
      std::pair<pred_iterator, pred_iterator> prev_blocks() 
      { 
        return std::make_pair (m_prev.begin (), m_prev.end ());
      }
      
      std::pair<const_succ_iterator,const_succ_iterator> next_blocks () const
      { 
        return std::make_pair (m_next.begin (), m_next.end ());
      }
      
      std::pair<const_pred_iterator,const_pred_iterator> prev_blocks() const
      { 
        return std::make_pair (m_prev.begin (), m_prev.end ());
      }

      // void reverse()
      // {
      //   std::swap (m_prev, m_next);
      //   std::reverse (m_stmts.begin (), m_stmts.end ());
      // }
      
      // Add a cfg edge from *this to b
      void operator>>(basic_block_t& b) 
      {
        insert_adjacent (m_next, b.m_bb_id);
        insert_adjacent (b.m_prev, m_bb_id);
      }
      
      // Remove a cfg edge from *this to b
      void operator-=(basic_block_t &b)
      {
        remove_adjacent (m_next, b.m_bb_id);
        remove_adjacent (b.m_prev, m_bb_id);       
      }
      
      // insert all statements of other at the front
      void merge_front (const basic_block_t &other) 
      {
        m_stmts.insert (m_stmts.begin (), 
                        other.m_stmts.begin (), 
                        other.m_stmts.end ());

        m_live = m_live | other.m_live;
      }
      
      // insert all statements of other at the back
      void merge_back (const basic_block_t &other) 
      {
        m_stmts.insert (m_stmts.end (), 
                        other.m_stmts.begin (), 
                        other.m_stmts.end ());

        m_live = m_live | other.m_live;
      }
      
      void write(crab_os& o) const {
        o << cfg_impl::get_label_str (m_bb_id) << ":\n";	
        for (auto const &s: *this) {
          o << "  " << s << ";\n";
	}
	std::pair<const_succ_iterator, const_succ_iterator> p = next_blocks();
	const_succ_iterator it = p.first;
	const_succ_iterator et = p.second;
	if (it != et) {
	  o << "  " << "goto ";
	  for (; it != et; ) {
	    o << cfg_impl::get_label_str (*it);
	    ++it;
	    if (it == et) {
	      o << ";";
	    } else {
	      o << ",";
	    }
	  }
	}
	o << "\n";
        
        return;
      }

      /// To build statements
      
      void add (variable_t lhs, variable_t op1, variable_t op2) {
        insert(boost::static_pointer_cast< statement_t, bin_op_t >
               (boost::make_shared<bin_op_t>(lhs, BINOP_ADD, op1, op2)));
      }
      
      void add (variable_t lhs, variable_t op1, Number op2) {
        insert(boost::static_pointer_cast< statement_t, bin_op_t > 
               (boost::make_shared<bin_op_t>(lhs, BINOP_ADD, op1,  op2)));
      }
      
      void sub (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_SUB, op1, op2)));
      }
      
      void sub (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_SUB, op1, op2)));
      }
      
      void mul (variable_t lhs, variable_t op1, variable_t op2) {
        insert(boost::static_pointer_cast< statement_t, bin_op_t >
               (boost::make_shared<bin_op_t>(lhs, BINOP_MUL, op1, op2)));
      }
      
      void mul (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_MUL, op1, op2)));
      }
      
      // signed division
      void div (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_SDIV, op1, op2)));
      }
      
      void div (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_SDIV, op1, op2)));
      }
      
      // unsigned division
      void udiv (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_UDIV, op1, op2)));
      }
      
      void udiv (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_UDIV, op1, op2)));
      }
      
      // signed rem
      void rem (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_SREM, op1, op2)));
      }
      
      void rem (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_SREM, op1, op2)));
      }
      
      // unsigned rem
      void urem (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_UREM, op1, op2)));
      }
      
      void urem (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_UREM, op1, op2)));
      }
      
      void bitwise_and (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_AND, op1, op2)));
      }
      
      void bitwise_and (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t> (lhs, BINOP_AND, op1, op2)));
      }
      
      void bitwise_or (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t> (lhs, BINOP_OR, op1, op2)));
      }
      
      void bitwise_or (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_OR, op1, op2)));
      }
      
      void bitwise_xor (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_XOR, op1, op2)));
      }
      
      void bitwise_xor (variable_t lhs, variable_t op1, Number op2) {
        insert (boost::static_pointer_cast< statement_t, bin_op_t >
                (boost::make_shared<bin_op_t>(lhs, BINOP_XOR, op1, op2)));
      }
            
      void assign (variable_t lhs, lin_exp_t rhs) {
        insert (boost::static_pointer_cast< statement_t, assign_t >
                (boost::make_shared<assign_t> (lhs, rhs)));
      }

      void assume (lin_cst_t cst) {
        insert (boost::static_pointer_cast< statement_t, assume_t >
                (boost::make_shared<assume_t> (cst)));
      }
      
      void havoc(variable_t lhs) {
        insert (boost::static_pointer_cast< statement_t, havoc_t > 
                (boost::make_shared<havoc_t> (lhs)));
      }
      
      void unreachable() {
        insert (boost::static_pointer_cast< statement_t, unreach_t > 
                (boost::make_shared<unreach_t> ()));
      }
      
      void select (variable_t lhs, variable_t v, lin_exp_t e1, lin_exp_t e2) {
        lin_cst_t cond = (v >= Number(1));
        insert(boost::static_pointer_cast< statement_t, select_t >
               (boost::make_shared<select_t>(lhs, cond, e1, e2)));
      }
      
      void select (variable_t lhs, lin_cst_t cond, lin_exp_t e1, lin_exp_t e2) {
        insert(boost::static_pointer_cast< statement_t, select_t >
               (boost::make_shared<select_t>(lhs, cond, e1, e2)));
      }

      void assertion (lin_cst_t cst, debug_info di = debug_info ()) {
	insert (boost::static_pointer_cast< statement_t, assert_t >
		(boost::make_shared<assert_t> (cst, di)));
      }

      void truncate(variable_t src, variable_t dst) {
	insert (boost::static_pointer_cast<statement_t, int_cast_t >
		(boost::make_shared<int_cast_t>(CAST_TRUNC,src,dst)));
      }
      
      void sext(variable_t src, variable_t dst) {
	insert (boost::static_pointer_cast<statement_t, int_cast_t >
		(boost::make_shared<int_cast_t>(CAST_SEXT,src,dst)));
      }

      void zext(variable_t src, variable_t dst) {
	insert (boost::static_pointer_cast<statement_t, int_cast_t >
		(boost::make_shared<int_cast_t>(CAST_ZEXT,src,dst)));
      }
      
      void callsite (std::string func, 
		     const std::vector<variable_t> &lhs, 
                     const std::vector<variable_t> &args) { 
        insert(boost::static_pointer_cast< statement_t, callsite_t >
               (boost::make_shared<callsite_t>(func, lhs, args)));
      }
            
      void ret (variable_t var) {
        std::vector<variable_t> ret_vals = {var};
        insert(boost::static_pointer_cast< statement_t, return_t >
               (boost::make_shared<return_t>(ret_vals)));
      }

      void ret (const std::vector<variable_t> &ret_vals) {
        insert(boost::static_pointer_cast< statement_t, return_t >
               (boost::make_shared<return_t>(ret_vals)));
      }
            

      void array_assume (variable_t a, uint64_t elem_size,
                         lin_exp_t lb_idx, lin_exp_t ub_idx, lin_exp_t v) {
        if (m_track_prec == ARR)
          insert(boost::static_pointer_cast<statement_t, arr_assume_t> 
		(boost::make_shared<arr_assume_t>(a, elem_size, lb_idx, ub_idx, v)));
      }

      void array_store (variable_t arr, lin_exp_t idx, lin_exp_t v, 
                        uint64_t elem_size, bool is_singleton = false)  {
        if (m_track_prec == ARR)
          insert(boost::static_pointer_cast< statement_t, arr_store_t >
            (boost::make_shared<arr_store_t>(arr, idx, v, elem_size, is_singleton)));
      }

      void array_load (variable_t lhs, variable_t arr,
                       lin_exp_t idx, uint64_t elem_size) {
        if (m_track_prec == ARR)
          insert(boost::static_pointer_cast< statement_t, arr_load_t >
                 (boost::make_shared<arr_load_t>(lhs, arr, idx, elem_size)));
      }

      void array_assign (variable_t lhs, variable_t rhs) {
        if (m_track_prec == ARR)
          insert(boost::static_pointer_cast< statement_t, arr_assign_t >
                 (boost::make_shared<arr_assign_t>(lhs, rhs)));
      }
            
      void ptr_store (variable_t lhs, variable_t rhs) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_store_t >
                 (boost::make_shared<ptr_store_t> (lhs, rhs)));
      }
      
      void ptr_load (variable_t lhs, variable_t rhs) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_load_t >
                 (boost::make_shared<ptr_load_t> (lhs, rhs)));
      }
      
      void ptr_assign (variable_t lhs, variable_t rhs, lin_exp_t offset) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_assign_t >
                 (boost::make_shared<ptr_assign_t> (lhs, rhs, offset)));
      }
      
      void ptr_new_object (variable_t lhs, ikos::index_t address) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_object_t >
                 (boost::make_shared<ptr_object_t> (lhs, address)));
      }
      
      void ptr_new_func (variable_t lhs, ikos::index_t func) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_function_t >
                 (boost::make_shared<ptr_function_t> (lhs, func)));
      }

      void ptr_null (variable_t lhs) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_null_t >
                 (boost::make_shared<ptr_null_t> (lhs)));
      }

      void ptr_assume (pointer_constraint<variable_t> cst) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_assume_t >
                 (boost::make_shared<ptr_assume_t> (cst)));
      }

      void ptr_assertion (pointer_constraint<variable_t> cst) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_assert_t >
                 (boost::make_shared<ptr_assert_t> (cst)));
      }

      void ptr_assertion (pointer_constraint<variable_t> cst, debug_info di) {
        if (m_track_prec >= PTR)
          insert(boost::static_pointer_cast< statement_t, ptr_assert_t >
                 (boost::make_shared<ptr_assert_t> (cst, di)));
      }


      void bool_assign (variable_t lhs, ikos::linear_constraint<Number, VariableName> rhs) {
        insert (boost::static_pointer_cast< statement_t, bool_assign_cst_t >
                (boost::make_shared<bool_assign_cst_t> (lhs, rhs)));
      }


      void bool_assign (variable_t lhs, variable_t rhs, bool is_not_rhs = false) {
        insert (boost::static_pointer_cast< statement_t, bool_assign_var_t >
                (boost::make_shared<bool_assign_var_t> (lhs, rhs, is_not_rhs)));
      }
      
      void bool_assume (variable_t c) {
        insert (boost::static_pointer_cast< statement_t, bool_assume_t >
                (boost::make_shared<bool_assume_t> (c, false)));
      }

      void bool_not_assume (variable_t c) {
        insert (boost::static_pointer_cast< statement_t, bool_assume_t >
                (boost::make_shared<bool_assume_t> (c, true)));
      }
      
      void bool_assert (variable_t c, debug_info di = debug_info ()) {
        insert (boost::static_pointer_cast< statement_t, bool_assert_t >
                (boost::make_shared<bool_assert_t> (c, di)));
      }

      void bool_select (variable_t lhs, variable_t cond, variable_t b1, variable_t b2) {
        insert(boost::static_pointer_cast< statement_t, bool_select_t >
               (boost::make_shared<bool_select_t>(lhs, cond, b1, b2)));
      }
      
      void bool_and (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bool_bin_op_t >
                (boost::make_shared<bool_bin_op_t>(lhs, BINOP_BAND, op1, op2)));
      }

      void bool_or (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bool_bin_op_t >
                (boost::make_shared<bool_bin_op_t>(lhs, BINOP_BOR, op1, op2)));
      }

      void bool_xor (variable_t lhs, variable_t op1, variable_t op2) {
        insert (boost::static_pointer_cast< statement_t, bool_bin_op_t >
                (boost::make_shared<bool_bin_op_t>(lhs, BINOP_BXOR, op1, op2)));
      }
      
      friend crab_os& operator<<(crab_os &o, const basic_block_t &b) {
        //b.write (o);
        o << cfg_impl::get_label_str (b);
        return o;
      }
      
    }; 

    // Viewing a BasicBlock with all statements reversed. Useful for
    // backward analysis.
    template<class BasicBlock> 
    class basic_block_rev {
     public:
      typedef typename BasicBlock::number_t number_t;      
      typedef typename BasicBlock::varname_t varname_t;
      typedef typename BasicBlock::variable_t variable_t;
      typedef typename BasicBlock::basic_block_label_t basic_block_label_t;

      typedef basic_block_rev<BasicBlock> basic_block_rev_t;

      typedef typename BasicBlock::succ_iterator succ_iterator;
      typedef typename BasicBlock::const_succ_iterator const_succ_iterator;
      typedef succ_iterator pred_iterator;
      typedef const_succ_iterator const_pred_iterator;

      typedef typename BasicBlock::reverse_iterator iterator;
      typedef typename BasicBlock::const_reverse_iterator const_iterator;
      typedef ikos::discrete_domain<variable_t> live_domain_t;
      
     private:

      BasicBlock& _bb;

     public:

      basic_block_rev(BasicBlock& bb): _bb(bb) {}

      basic_block_label_t label () const { return _bb.label(); }

      std::string name () const { return _bb.name();}

      iterator begin() { return _bb.rbegin();}            

      iterator end() { return _bb.rend();}             

      const_iterator begin() const { return _bb.rbegin(); }

      const_iterator end() const { return _bb.rend();}
      
      std::size_t size() const { return std::distance ( begin (), end ()); }

      void accept(statement_visitor<number_t, varname_t> *v) {
	v->visit(*this);
      }
      
      live_domain_t& live () { return _bb.live(); }

      live_domain_t live () const { return _bb.live(); }

      std::pair<succ_iterator, succ_iterator> next_blocks()
      { return _bb.prev_blocks(); }
      
      std::pair<pred_iterator, pred_iterator> prev_blocks() 
      { return _bb.next_blocks(); }
      
      std::pair<const_succ_iterator,const_succ_iterator> next_blocks () const
      { return _bb.prev_blocks(); }
      
      std::pair<const_pred_iterator,const_pred_iterator> prev_blocks() const
      { return _bb.next_blocks(); }

      void write(crab_os& o) const
      {
        o << name() << ":\n";	
        for (auto const &s: *this)
        { o << "  " << s << ";\n"; }
        o << "--> [";
        for (auto const &n : boost::make_iterator_range (next_blocks ()))
        { o << n << ";"; }
        o << "]\n";
        return;
      }     

      friend crab_os& operator<<(crab_os &o, const basic_block_rev_t &b) {
        //b.write (o);
        o << b.name();
        return o;
      }
    };
  
    template< class Number, class VariableName>
    struct statement_visitor
    {

      typedef binary_op<Number,VariableName> bin_op_t;
      typedef assignment<Number,VariableName> assign_t;
      typedef assume_stmt<Number,VariableName> assume_t;
      typedef select_stmt<Number,VariableName> select_t;
      typedef assert_stmt<Number,VariableName> assert_t;
      typedef int_cast_stmt<Number, VariableName> int_cast_t;      

      typedef havoc_stmt<Number, VariableName> havoc_t;
      typedef unreachable_stmt<Number, VariableName> unreach_t;
      
      typedef callsite_stmt<Number, VariableName> callsite_t;
      typedef return_stmt<Number, VariableName> return_t;
      
      typedef array_assume_stmt<Number,VariableName> arr_assume_t;
      typedef array_store_stmt<Number,VariableName> arr_store_t;
      typedef array_load_stmt<Number,VariableName> arr_load_t;
      typedef array_assign_stmt<Number, VariableName> arr_assign_t;
      
      typedef ptr_store_stmt<Number, VariableName> ptr_store_t;
      typedef ptr_load_stmt<Number, VariableName> ptr_load_t;
      typedef ptr_assign_stmt<Number,VariableName> ptr_assign_t;
      typedef ptr_object_stmt<Number, VariableName> ptr_object_t;
      typedef ptr_function_stmt<Number, VariableName> ptr_function_t;
      typedef ptr_null_stmt<Number, VariableName> ptr_null_t;
      typedef ptr_assume_stmt<Number, VariableName> ptr_assume_t;
      typedef ptr_assert_stmt<Number, VariableName> ptr_assert_t;

      typedef bool_binary_op <Number,VariableName> bool_bin_op_t;
      typedef bool_assign_cst <Number,VariableName> bool_assign_cst_t;
      typedef bool_assign_var <Number,VariableName> bool_assign_var_t;      
      typedef bool_assume_stmt <Number,VariableName> bool_assume_t;
      typedef bool_select_stmt<Number,VariableName> bool_select_t;
      typedef bool_assert_stmt <Number,VariableName> bool_assert_t;

      virtual void visit (bin_op_t&) {};
      virtual void visit (assign_t&) {};
      virtual void visit (assume_t&) {};
      virtual void visit (select_t&) {};
      virtual void visit (assert_t&) {};
      virtual void visit (int_cast_t&) {};      

      virtual void visit (unreach_t&) {};
      virtual void visit (havoc_t&) {};
      
      virtual void visit (callsite_t&) {};
      virtual void visit (return_t&) {};

      virtual void visit (arr_assume_t&) {};
      virtual void visit (arr_store_t&) {};
      virtual void visit (arr_load_t&) {};
      virtual void visit (arr_assign_t&) {};

      virtual void visit (ptr_store_t&) {};
      virtual void visit (ptr_load_t&) {};
      virtual void visit (ptr_assign_t&) {};
      virtual void visit (ptr_object_t&) {};
      virtual void visit (ptr_function_t&) {};
      virtual void visit (ptr_null_t&) {};
      virtual void visit (ptr_assume_t&) {};
      virtual void visit (ptr_assert_t&) {};

      virtual void visit (bool_bin_op_t&) {};
      virtual void visit (bool_assign_cst_t&) {};
      virtual void visit (bool_assign_var_t&) {};      
      virtual void visit (bool_assume_t&) {};
      virtual void visit (bool_select_t&) {};
      virtual void visit (bool_assert_t&) {};

      template<typename BasicBlockLabel>
      void visit (basic_block<BasicBlockLabel, VariableName, Number> &b) {
	for (auto &s: b) {
	  s.accept(this);
	}
      }

      template<typename BasicBlock>
      void visit (basic_block_rev<BasicBlock> &b) {
	for (auto &s: b) {
	  s.accept(this);
	}
      }
      
      virtual ~statement_visitor () {}
    }; 
    
    template<class Number, class VariableName>
    class function_decl {
     public:

      typedef ikos::variable<Number, VariableName> variable_t;      
      typedef typename variable_t::type_t type_t;
      
     private:

      std::string m_func_name;
      std::vector<variable_t> m_inputs;
      std::vector<variable_t> m_outputs;      
      
      typedef typename std::vector<variable_t>::iterator param_iterator;
      typedef typename std::vector<variable_t>::const_iterator const_param_iterator;
      
     public:
      
      function_decl (std::string func_name,
                     std::vector<variable_t> inputs,
		     std::vector<variable_t> outputs)
	: m_func_name (func_name) {
	
        std::copy(inputs.begin(), inputs.end(),
		  std::back_inserter(m_inputs));	
        std::copy(outputs.begin(), outputs.end(),
		  std::back_inserter(m_outputs));

	// CFG restriction: inputs and outputs must be disjoint,
	// otherwise we cannot produce meaningful input-output
	// relations.
	std::set<variable_t> s;
	for(auto &tv: m_inputs) {s.insert(tv);}
	for(auto &tv: m_outputs){s.insert(tv);}	
	if (s.size () != (m_inputs.size () + m_outputs.size ())) {
	  CRAB_ERROR("interprocedural analysis requires that for each function ",
		     "its set of inputs and outputs must be disjoint.");
	}
      }

      std::string get_func_name () const
      { return m_func_name; }
      
      const std::vector<variable_t>& get_inputs () const
      { return m_inputs; }

      const std::vector<variable_t>& get_outputs () const
      { return m_outputs; }

      unsigned get_num_inputs () const
      { return m_inputs.size (); }
      
      unsigned get_num_outputs () const
      { return m_outputs.size (); }
      
      variable_t get_input_name (unsigned idx) const { 
        if (idx >= m_inputs.size ())
          CRAB_ERROR ("Out-of-bound access to function input parameter");
        return m_inputs[idx];
      }

      type_t get_input_type (unsigned idx) const { 
        if (idx >= m_inputs.size ())
          CRAB_ERROR ("Out-of-bound access to function output parameter");
        return m_inputs[idx].get_type();
      }
      
      variable_t get_output_name (unsigned idx) const { 
        if (idx >= m_outputs.size ())
          CRAB_ERROR ("Out-of-bound access to function input parameter");
        return m_outputs[idx];
      }

      type_t get_output_type (unsigned idx) const { 
        if (idx >= m_outputs.size ())
          CRAB_ERROR ("Out-of-bound access to function output parameter");
        return m_outputs[idx].get_type();
      }
      
      void write(crab_os& o) const {

        if (m_outputs.empty ()) {
          o << "void";
        } else if (m_outputs.size () == 1) {
	  auto out = *(m_outputs.begin());
          o << out << ":" << out.get_type();
        } else {
          o << "(";
          for (auto It = m_outputs.begin (),
		 Et=m_outputs.end (); It!=Et; ) {
	    auto out = *It;
            o << out << ":" << out.get_type();
            ++It;
            if (It != Et)
              o << ",";
          }
          o << ")";
        }

        o << " declare " << m_func_name << "(";
        for (const_param_iterator It = m_inputs.begin (),
	       Et=m_inputs.end (); It!=Et; ) {
          o << (*It) << ":" << (*It).get_type();
          ++It;
          if (It != Et)
            o << ",";
        }
        o << ")";
        
        return;
      }
      
      friend crab_os& operator<<(crab_os& o, const function_decl<Number, VariableName> &decl) {
        decl.write (o);
        return o;
      }
    }; 

    // forward declarations
    template<class Any> class cfg_rev;
    template<class Any> class cfg_ref;
         
    template< class BasicBlockLabel, class VariableName, class Number>
    class Cfg: public boost::noncopyable {
     public:

      typedef Number number_t; 
      typedef BasicBlockLabel basic_block_label_t;
      typedef basic_block_label_t node_t; // for Bgl graphs
      typedef VariableName varname_t;
      typedef ikos::variable<number_t, varname_t> variable_t;
      typedef function_decl<number_t, varname_t> fdecl_t;
      typedef basic_block<BasicBlockLabel, VariableName, number_t> basic_block_t;   
      typedef statement<number_t, VariableName > statement_t;

      typedef typename basic_block_t::succ_iterator succ_iterator;
      typedef typename basic_block_t::pred_iterator pred_iterator;
      typedef typename basic_block_t::const_succ_iterator const_succ_iterator;
      typedef typename basic_block_t::const_pred_iterator const_pred_iterator;
      
      typedef boost::iterator_range <succ_iterator> succ_range;
      typedef boost::iterator_range <pred_iterator> pred_range;
      typedef boost::iterator_range <const_succ_iterator> const_succ_range;
      typedef boost::iterator_range <const_pred_iterator> const_pred_range;
      
     private:
      
      typedef Cfg<BasicBlockLabel, VariableName, Number> cfg_t;
      typedef boost::shared_ptr< basic_block_t > basic_block_ptr;
      typedef boost::unordered_map< BasicBlockLabel, basic_block_ptr > basic_block_map_t;
      typedef typename basic_block_map_t::value_type binding_t;
      typedef typename basic_block_t::live_domain_t live_domain_t;

      struct get_ref : public std::unary_function<binding_t, basic_block_t>
      {
        get_ref () { }
        basic_block_t& operator () (const binding_t &p) const { return *(p.second); }
      }; 
      
      struct getLabel : public std::unary_function<binding_t, BasicBlockLabel>
      {
        getLabel () { }
        BasicBlockLabel operator () (const binding_t &p) const { return p.second->label(); }
      }; 
      
     public:
      
      typedef boost::transform_iterator< get_ref, 
                                         typename basic_block_map_t::iterator > iterator;
      typedef boost::transform_iterator< get_ref, 
                                         typename basic_block_map_t::const_iterator > const_iterator;
      typedef boost::transform_iterator< getLabel, 
                                         typename basic_block_map_t::iterator > label_iterator;
      typedef boost::transform_iterator< getLabel, 
                                         typename basic_block_map_t::const_iterator > const_label_iterator;

      typedef typename std::vector<varname_t>::iterator var_iterator;
      typedef typename std::vector<varname_t>::const_iterator const_var_iterator;

     private:
      
      BasicBlockLabel m_entry;
      BasicBlockLabel m_exit;
      bool m_has_exit;
      basic_block_map_t m_blocks;
      tracked_precision m_track_prec;
      //! we allow to define a cfg without being associated with a
      //! function
      boost::optional<fdecl_t> m_func_decl; 
      
      
      typedef boost::unordered_set< BasicBlockLabel > visited_t;
      template<typename T>
      void dfs_rec (BasicBlockLabel curId, visited_t &visited, T f) const
      {
        if (visited.find (curId) != visited.end ()) return;
        visited.insert (curId);
        
        const basic_block_t &cur = get_node (curId);
        f (cur);
        for (auto const n : boost::make_iterator_range (cur.next_blocks ()))
          dfs_rec (n, visited, f);
      }
      
      template<typename T>
      void dfs (T f) const 
      {
        visited_t visited;
        dfs_rec (m_entry, visited, f);
      }
      
      struct print_block 
      {
        crab_os &m_o;
        print_block (crab_os& o) : m_o (o) { }
        void operator () (const basic_block_t& B){ B.write (m_o); }
      };
      
      
     public:
      
      // --- needed by crab::cg::call_graph<CFG>::cg_node
      Cfg () { }
      
      Cfg (BasicBlockLabel entry, tracked_precision track_prec = NUM)
          : m_entry  (entry),  
            m_has_exit (false),
            m_track_prec (track_prec) {
        m_blocks.insert (binding_t (m_entry, 
                                    basic_block_t::create (m_entry, m_track_prec)));
      }
      
      Cfg (BasicBlockLabel entry, BasicBlockLabel exit, 
           tracked_precision track_prec = NUM)
          : m_entry  (entry), 
            m_exit   (exit), 
            m_has_exit (true),
            m_track_prec (track_prec) {
        m_blocks.insert (binding_t (m_entry, 
                                    basic_block_t::create (m_entry, m_track_prec)));
      }
      
      Cfg (BasicBlockLabel entry, BasicBlockLabel exit, 
           fdecl_t func_decl, 
           tracked_precision track_prec = NUM)
          : m_entry (entry), 
            m_exit (exit), 
            m_has_exit (true),
            m_track_prec (track_prec),
            m_func_decl (boost::optional<fdecl_t> (func_decl)) {
        m_blocks.insert (binding_t (m_entry, 
                                    basic_block_t::create (m_entry, m_track_prec)));
      }
      
      boost::shared_ptr<cfg_t> clone () const
      {
        boost::shared_ptr<cfg_t> _cfg (new cfg_t (m_entry, m_track_prec));
        _cfg->m_has_exit = m_has_exit ;
        if (_cfg->m_has_exit)
          _cfg->m_exit = m_exit ;
        _cfg->m_func_decl = m_func_decl;
        for (auto const &BB: boost::make_iterator_range (begin (), end ()))
        {
          boost::shared_ptr <basic_block_t> copyBB = BB.clone ();
          _cfg->m_blocks.insert (binding_t (copyBB->label (), copyBB));
        }
        return _cfg;
      }
      
      boost::optional<fdecl_t> get_func_decl () const { 
        return m_func_decl; 
      }
      
      tracked_precision get_track_prec () const {
        return m_track_prec;
      }
      
      
      bool has_exit () const { return m_has_exit; }
      
      BasicBlockLabel exit()  const { 
        if (has_exit ()) return m_exit; 
        CRAB_ERROR ("cfg does not have an exit block");
      } 
      
      //! set method to mark the exit block after the cfg has been
      //! created.
      void set_exit (BasicBlockLabel exit) { 
        m_exit = exit; 
        m_has_exit = true;
      }
      
      //! set method to add the function declaration after the cfg has
      //! been created.
      void set_func_decl (fdecl_t decl) { 
        m_func_decl  = boost::optional<fdecl_t> (decl);
      }

      // --- Begin ikos fixpoint API

      BasicBlockLabel entry() const { return m_entry; } 

      const_succ_range next_nodes (BasicBlockLabel bb_id) const
      {
        const basic_block_t& b = get_node(bb_id);
        return boost::make_iterator_range (b.next_blocks ());
      }
      
      const_pred_range prev_nodes (BasicBlockLabel bb_id) const
      {
        const basic_block_t& b = get_node(bb_id);
        return boost::make_iterator_range (b.prev_blocks ());
      }
      
      succ_range next_nodes (BasicBlockLabel bb_id) 
      {
        basic_block_t& b = get_node(bb_id);
        return boost::make_iterator_range (b.next_blocks ());
      }
      
      pred_range prev_nodes (BasicBlockLabel bb_id) 
      {
        basic_block_t& b = get_node(bb_id);
        return boost::make_iterator_range (b.prev_blocks ());
      }

      basic_block_t& get_node (BasicBlockLabel bb_id) 
      {
        auto it = m_blocks.find (bb_id);
        if (it == m_blocks.end ())
          CRAB_ERROR ("Basic block ", bb_id, " not found in the CFG: ",__LINE__);
        
        return *(it->second);
      }
      
      const basic_block_t& get_node (BasicBlockLabel bb_id) const
      {
        auto it = m_blocks.find (bb_id);
        if (it == m_blocks.end ())
          CRAB_ERROR ("Basic block ", bb_id, " not found in the CFG: ",__LINE__);	  
        
        return *(it->second);
      }

      // --- End ikos fixpoint API

      basic_block_t& insert (BasicBlockLabel bb_id) 
      {
        auto it = m_blocks.find (bb_id);
        if (it != m_blocks.end ()) return *(it->second);
        
        basic_block_ptr block = basic_block_t::create (bb_id, m_track_prec);
        m_blocks.insert (binding_t (bb_id, block));
        return *block;
      }
      
      void remove (BasicBlockLabel bb_id)
      {
        basic_block_t& bb = get_node(bb_id) ;
        
        std::vector< std::pair<basic_block_t*,basic_block_t*> > dead;
        
        for (auto id : boost::make_iterator_range (bb.prev_blocks ()))
        { 
          if (bb_id != id)
          {
            basic_block_t& p = get_node(id) ;
            dead.push_back (std::make_pair (&p,&bb));
          }
        }
        
        for (auto id : boost::make_iterator_range (bb.next_blocks ()))
        {
          if (bb_id != id)
          {
            basic_block_t& s = get_node(id) ;
            dead.push_back (std::make_pair (&bb,&s));
          }
        }
        
        for (auto p : dead)
          (*p.first) -= (*p.second);
        
        m_blocks.erase (bb_id);
      }
      
      // Return all variables (either used or defined) in the cfg.
      //
      // This operation is linear on the size of the cfg to still keep
      // a valid set in case a block is removed.
      std::vector<varname_t> get_vars () const {
        live_domain_t ls = live_domain_t::bottom ();
        for (auto const &b : boost::make_iterator_range (begin (), end ()))
          ls = ls | b.live ();
        // std::vector<varname_t> vars (ls.size ());
        // vars.insert (vars.end (), ls.begin (), ls.end ());
        std::vector<varname_t> vars;
        for (auto v: ls) vars.push_back (v);
        return vars;
      }
            
      //! return a begin iterator of BasicBlock's
      iterator begin() 
      {
        return boost::make_transform_iterator (m_blocks.begin (), get_ref ());
      }
      
      //! return an end iterator of BasicBlock's
      iterator end() 
      {
        return boost::make_transform_iterator (m_blocks.end (), get_ref ());
      }
      
      const_iterator begin() const
      {
        return boost::make_transform_iterator (m_blocks.begin (), get_ref ());
      }
      
      const_iterator end() const
      {
        return boost::make_transform_iterator (m_blocks.end (), get_ref ());
      }
      
      //! return a begin iterator of BasicBlockLabel's
      label_iterator label_begin() 
      {
        return boost::make_transform_iterator (m_blocks.begin (), getLabel ());
      }
      
      //! return an end iterator of BasicBlockLabel's
      label_iterator label_end() 
      {
        return boost::make_transform_iterator (m_blocks.end (), getLabel ());
      }
      
      const_label_iterator label_begin() const
      {
        return boost::make_transform_iterator (m_blocks.begin (), getLabel ());
      }
      
      const_label_iterator label_end() const
      {
        return boost::make_transform_iterator (m_blocks.end (), getLabel ());
      }
      
      size_t size () const { return std::distance (begin (), end ()); }
     
      void write (crab_os& o) const
      {
        print_block f (o);
        if (m_func_decl)
          o << *m_func_decl << "\n";
        dfs (f);
        return;
      }
      
      friend crab_os& operator<<(crab_os &o, 
                                 const Cfg< BasicBlockLabel, VariableName, Number> &cfg)
      {
        cfg.write (o);
        return o;
      }
      
      void simplify() {
        merge_blocks();        
        remove_unreachable_blocks();
        remove_useless_blocks();
        //after removing useless blocks there can be opportunities to
        //merge more blocks.
        merge_blocks();
        merge_blocks();
      }

    private:
      
      ////
      // cfg simplifications
      ////
      
      //XXX: this is a bit adhoc. It should be probably a parameter of
      //     simplify().
      struct donot_simplify_visitor: public statement_visitor<number_t, VariableName>
      {
        typedef typename statement_visitor<number_t, VariableName>::havoc_t havoc_t;
        typedef typename statement_visitor<number_t, VariableName>::bin_op_t bin_op_t;
        typedef typename statement_visitor<number_t, VariableName>::assign_t assign_t;
        typedef typename statement_visitor<number_t, VariableName>::assume_t assume_t;
        typedef typename statement_visitor<number_t, VariableName>::unreach_t unreach_t;
        typedef typename statement_visitor<number_t, VariableName>::select_t select_t;
        typedef typename statement_visitor<number_t, VariableName>::arr_load_t arr_load_t;
        typedef typename statement_visitor<number_t, VariableName>::bool_assume_t bool_assume_t;
        
        bool _do_not_simplify;
        donot_simplify_visitor (): _do_not_simplify(false) { }
        void visit(bin_op_t&){ }  
        void visit(assign_t&) { }
        void visit(assume_t&) { _do_not_simplify = true; }
        void visit(bool_assume_t&) { _do_not_simplify = true; }	
        void visit(arr_load_t&) { _do_not_simplify = true; }
        void visit(havoc_t&) { }
        void visit(unreach_t&){ }
        void visit(select_t&){ }
      };
      
      // Helpers
      bool has_one_child (BasicBlockLabel b)
      {
        auto rng = next_nodes (b);
        return (std::distance (rng.begin (), rng.end ()) == 1);
      }
      
      bool has_one_parent (BasicBlockLabel b)
      {
        auto rng = prev_nodes (b);
        return (std::distance (rng.begin (), rng.end ()) == 1);
      }
      
      basic_block_t& get_child (BasicBlockLabel b)
      {
        assert (has_one_child (b));
        auto rng = next_nodes (b);
        return get_node (*(rng.begin ()));
      }
      
      basic_block_t& get_parent (BasicBlockLabel b)
      {
        assert (has_one_parent (b));
        auto rng = prev_nodes (b);
        return get_node (*(rng.begin ()));
      }
      
      void merge_blocks_rec (BasicBlockLabel curId, 
                           visited_t& visited)
      {
        
        if (visited.find (curId) != visited.end ()) return;
        visited.insert (curId);
        
        basic_block_t &cur = get_node (curId);
        
        if (has_one_child (curId) && has_one_parent (curId))
        {
          basic_block_t &parent = get_parent (curId);
          basic_block_t &child  = get_child (curId);
          
          donot_simplify_visitor vis;
          for (auto it = cur.begin (); it != cur.end (); ++it)
            it->accept(&vis);
          
          if (!vis._do_not_simplify)
          {
            parent.merge_back (cur);
            remove (curId);
            parent >> child;        
            merge_blocks_rec (child.label (), visited); 
            return;
          }
        }
        
        for (auto n : boost::make_iterator_range (cur.next_blocks ()))
          merge_blocks_rec (n, visited);
      }
      
      // Merges a basic block into its predecessor if there is only one
      // and the predecessor only has one successor.
      void merge_blocks ()
      {
        visited_t visited;
        merge_blocks_rec (entry (), visited);
      }
      
      // mark reachable blocks from curId
      template<class AnyCfg>
      void mark_alive_blocks (BasicBlockLabel curId, 
                            AnyCfg& cfg,
                            visited_t& visited)
      {
        if (visited.count (curId) > 0) return;
        visited.insert (curId);
        for (auto child : cfg.next_nodes (curId))
          mark_alive_blocks (child, cfg, visited);
      }
      
      // remove unreachable blocks
      void remove_unreachable_blocks ()
      {
        visited_t alive, dead;
        mark_alive_blocks (entry (), *this, alive);
        
        for (auto const &bb : *this) 
          if (!(alive.count (bb.label ()) > 0))
            dead.insert (bb.label ());
        
        for (auto bb_id: dead)
          remove (bb_id);
      }
      
      // remove blocks that cannot reach the exit block
      void remove_useless_blocks ()
      {
        if (!has_exit ()) return;
        
        cfg_rev<cfg_ref<cfg_t> > rev_cfg (*this); 

        visited_t useful, useless;
        mark_alive_blocks (rev_cfg.entry (), rev_cfg, useful);
        
        for (auto const &bb : *this) 
          if (!(useful.count (bb.label ()) > 0))
            useless.insert (bb.label ());
        
        for (auto bb_id: useless)
          remove (bb_id);
      }
      
    }; 

    // A lightweight object that wraps a reference to a CFG into a
    // copyable, assignable object.
    template <class CFG>
    class cfg_ref {
     public:

      // CFG's typedefs
      typedef typename CFG::basic_block_label_t basic_block_label_t;
      typedef typename CFG::node_t node_t;
      typedef typename CFG::varname_t varname_t;
      typedef typename CFG::number_t number_t;
      typedef typename CFG::variable_t variable_t;            
      typedef typename CFG::fdecl_t fdecl_t;
      typedef typename CFG::basic_block_t basic_block_t;   
      typedef typename CFG::statement_t statement_t;

      typedef typename CFG::succ_iterator succ_iterator;
      typedef typename CFG::pred_iterator pred_iterator;
      typedef typename CFG::const_succ_iterator const_succ_iterator;
      typedef typename CFG::const_pred_iterator const_pred_iterator;
      typedef typename CFG::succ_range succ_range;
      typedef typename CFG::pred_range pred_range;
      typedef typename CFG::const_succ_range const_succ_range;
      typedef typename CFG::const_pred_range const_pred_range;
      typedef typename CFG::iterator iterator;
      typedef typename CFG::const_iterator const_iterator;
      typedef typename CFG::label_iterator label_iterator;
      typedef typename CFG::const_label_iterator const_label_iterator;
      typedef typename CFG::var_iterator var_iterator;
      typedef typename CFG::const_var_iterator const_var_iterator;

     private:

      boost::optional<std::reference_wrapper<CFG> > _ref;

     public:

      // --- hook needed by crab::cg::CallGraph<CFG>::CgNode
      cfg_ref () { } 

      cfg_ref (CFG &cfg)
          : _ref(std::reference_wrapper<CFG>(cfg)) { } 
      
      const CFG& get() const { 
        assert (_ref);
        return *_ref;
      }

      CFG& get() { 
        assert (_ref);
        return *_ref;
      }

      basic_block_label_t  entry() const {
        assert (_ref);
        return (*_ref).get().entry();
      }

      const_succ_range next_nodes (basic_block_label_t bb) const {
        assert (_ref);
        return (*_ref).get().next_nodes(bb);
      }

      const_pred_range prev_nodes (basic_block_label_t bb) const {
        assert (_ref);
        return (*_ref).get().prev_nodes(bb);
      }

      succ_range next_nodes (basic_block_label_t bb) {
        assert (_ref);
        return (*_ref).get().next_nodes(bb);
      }

      pred_range prev_nodes (basic_block_label_t bb) {
        assert (_ref);
        return (*_ref).get().prev_nodes(bb);
      }

      basic_block_t& get_node (basic_block_label_t bb) {
        assert (_ref);
        return (*_ref).get().get_node(bb);
      }
      
      const basic_block_t& get_node (basic_block_label_t bb) const {
        assert (_ref);
        return (*_ref).get().get_node(bb);
      }

      size_t size () const {
        assert (_ref);
        return (*_ref).get().size();
      }
      
      iterator begin() {
        assert (_ref);
        return (*_ref).get().begin();
      }
      
      iterator end() {
        assert (_ref);
        return (*_ref).get().end();
      }
      
      const_iterator begin() const {
        assert (_ref);
        return (*_ref).get().begin();
      }
      
      const_iterator end() const {
        assert (_ref);
        return (*_ref).get().end();
      }

      label_iterator label_begin() {
        assert (_ref);
        return (*_ref).get().label_begin();
      }
      
      label_iterator label_end() {
        assert (_ref);
        return (*_ref).get().label_end();
      }
      
      const_label_iterator label_begin() const {
        assert (_ref);
        return (*_ref).get().label_begin();
      }
      
      const_label_iterator label_end() const {
        assert (_ref);
        return (*_ref).get().label_end();
      }

      boost::optional<fdecl_t> get_func_decl () const { 
        assert (_ref);
        return (*_ref).get().get_func_decl();
      }

      
      bool has_exit () const {
        assert (_ref);
        return (*_ref).get().has_exit();
      }
      
      basic_block_label_t exit()  const { 
        assert (_ref);
        return (*_ref).get().exit();
      }

      
      friend crab_os& operator<<(crab_os &o, const cfg_ref<CFG> &cfg) {
        o << cfg.get();
        return o;
      }

      void simplify () {
        if (_ref) (*_ref).get().simplify();
      }

      // #include <boost/fusion/functional/invocation/invoke.hpp>
      // template< class... ArgTypes >
      // typename std::result_of<CFG&(ArgTypes&&...)>::type
      // operator() ( ArgTypes&&... args ) const {
      //   return boost::fusion::invoke(get(), std::forward<ArgTypes>(args)...);
      // }      
    };
  
    // Viewing a CFG with all edges and block statements
    // reversed. Useful for backward analysis.
    template<class CFGRef> // CFGRef must be copyable!
    class cfg_rev {
     public:
      typedef typename CFGRef::basic_block_label_t basic_block_label_t;
      typedef basic_block_rev<typename CFGRef::basic_block_t> basic_block_t;
      typedef basic_block_label_t node_t; // for Bgl graphs
      typedef typename CFGRef::varname_t varname_t;
      typedef typename CFGRef::number_t number_t;
      typedef typename CFGRef::variable_t variable_t;
      typedef typename CFGRef::fdecl_t fdecl_t;
      typedef typename CFGRef::statement_t statement_t;

      typedef typename CFGRef::succ_range pred_range;
      typedef typename CFGRef::pred_range succ_range;
      typedef typename CFGRef::const_succ_range const_pred_range;
      typedef typename CFGRef::const_pred_range const_succ_range;

      // For BGL
      typedef typename basic_block_t::succ_iterator succ_iterator;
      typedef typename basic_block_t::pred_iterator pred_iterator;
      typedef typename basic_block_t::const_succ_iterator const_succ_iterator;
      typedef typename basic_block_t::const_pred_iterator const_pred_iterator;

      typedef cfg_rev<CFGRef> cfg_rev_t;

     private:

      struct getRev: public std::unary_function<typename CFGRef::basic_block_t, basic_block_t> {
        const boost::unordered_map<basic_block_label_t, basic_block_t>& _rev_bbs;

        getRev(const boost::unordered_map<basic_block_label_t, basic_block_t>& rev_bbs)
            : _rev_bbs(rev_bbs) { }

        const basic_block_t& operator()(typename CFGRef::basic_block_t &bb) const {
          auto it = _rev_bbs.find(bb.label());
          if (it != _rev_bbs.end())
            return it->second;
          CRAB_ERROR ("Basic block ", bb.label(), " not found in the CFG: ",__LINE__);
        }
      }; 

     public:

      typedef boost::transform_iterator<getRev, typename CFGRef::iterator> iterator;
      typedef boost::transform_iterator<getRev, typename CFGRef::const_iterator> const_iterator;
      typedef typename CFGRef::label_iterator label_iterator;
      typedef typename CFGRef::const_label_iterator const_label_iterator;
      typedef typename CFGRef::var_iterator var_iterator;
      typedef typename CFGRef::const_var_iterator const_var_iterator;

     private:

      CFGRef _cfg;
      boost::unordered_map<basic_block_label_t, basic_block_t> _rev_bbs;
     
     public:

      // --- hook needed by crab::cg::CallGraph<CFGRef>::CgNode
      cfg_rev () { }

      cfg_rev (CFGRef cfg): _cfg(cfg) { 
        // Create basic_block_rev from BasicBlock objects
        // Note that basic_block_rev is also a view of BasicBlock so it
        // doesn't modify BasicBlock objects.
        for(auto &bb: cfg) {
          basic_block_t rev(bb);
          _rev_bbs.insert(std::make_pair(bb.label(), rev));
        }
      }

      cfg_rev(const cfg_rev_t& o)
          : _cfg(o._cfg), _rev_bbs(o._rev_bbs) { }

      cfg_rev(cfg_rev_t && o)
          : _cfg(std::move(o._cfg)), _rev_bbs(std::move(o._rev_bbs)) { }

      cfg_rev_t& operator=(const cfg_rev_t&o) {
        if (this != &o) {
          _cfg = o._cfg;
          _rev_bbs = o._rev_bbs;
        }
        return *this;
      }

      cfg_rev_t& operator=(cfg_rev_t&&o) {
        _cfg = std::move(o._cfg);
        _rev_bbs = std::move(o._rev_bbs);
        return *this;
      }

      basic_block_label_t  entry() const {
        if (!_cfg.has_exit()) CRAB_ERROR("Entry not found!");
        return _cfg.exit();
      }

      const_succ_range next_nodes (basic_block_label_t bb) const {
        return _cfg.prev_nodes(bb);
      }

      const_pred_range prev_nodes (basic_block_label_t bb) const {
        return _cfg.next_nodes(bb);
      }

      succ_range next_nodes (basic_block_label_t bb) {
        return _cfg.prev_nodes(bb);
      }

      pred_range prev_nodes (basic_block_label_t bb) {
        return _cfg.next_nodes(bb);
      }

      basic_block_t& get_node (basic_block_label_t bb_id) {
        auto it = _rev_bbs.find (bb_id);
        if (it == _rev_bbs.end()) 
          CRAB_ERROR ("Basic block ", bb_id, " not found in the CFG: ",__LINE__);
        return it->second;
      }

      const basic_block_t& get_node (basic_block_label_t bb_id) const {
        auto it = _rev_bbs.find (bb_id);
        if (it == _rev_bbs.end()) 
          CRAB_ERROR ("Basic block ", bb_id, " not found in the CFG: ",__LINE__);
        return it->second;
      }
      
      iterator begin() {
        return boost::make_transform_iterator (_cfg.begin(), getRev(_rev_bbs));
      }
      
      iterator end() {
        return boost::make_transform_iterator (_cfg.end(), getRev(_rev_bbs));
      }
      
      const_iterator begin() const {
        return boost::make_transform_iterator (_cfg.begin(), getRev(_rev_bbs));
      }
      
      const_iterator end() const {
        return boost::make_transform_iterator (_cfg.end(), getRev(_rev_bbs));
      }

      label_iterator label_begin() {
        return _cfg.label_begin();
      }
      
      label_iterator label_end() {
        return _cfg.label_end();
      }
      
      const_label_iterator label_begin() const {
        return _cfg.label_begin();
      }
      
      const_label_iterator label_end() const {
        return _cfg.label_end();
      }

      boost::optional<fdecl_t> get_func_decl () const { 
        return _cfg.get_func_decl();
      }
      
      bool has_exit () const {
        return true;
      }
      
      basic_block_label_t exit()  const { 
        return _cfg.entry();
      }

      void write(crab_os& o) const {
        if (get_func_decl())
          o << *get_func_decl() << "\n";
        for (auto &bb: *this)
        { bb.write(o); }
      }

      friend crab_os& operator<<(crab_os &o, const cfg_rev<CFGRef> &cfg) {
        cfg.write(o);
        return o;
      }

      void simplify() { }
      
    };

     // Helper class
    template<typename CFG>
    struct cfg_hasher {
      typedef typename CFG::basic_block_t::callsite_t callsite_t;
      typedef typename CFG::fdecl_t fdecl_t;
      
      static size_t hash(callsite_t cs) {
	size_t res = boost::hash_value(cs.get_func_name());
        for (auto vt: cs.get_lhs ())
          boost::hash_combine(res, vt.get_type());
        for(unsigned i=0; i<cs.get_num_args (); i++)
          boost::hash_combine(res, cs.get_arg_type (i));
        return res;
      }
      
      static size_t hash(fdecl_t d)  {
        size_t res = boost::hash_value(d.get_func_name ());
        for(unsigned i=0; i<d.get_num_inputs (); i++)
          boost::hash_combine(res, d.get_input_type (i));
        for(unsigned i=0; i<d.get_num_outputs (); i++)
          boost::hash_combine(res, d.get_output_type (i));
	
        return res;
      }      
    };


    template<class CFG>
    class type_checker {

    public:

      type_checker(CFG cfg): m_cfg(cfg) {}
      
      void run() {
	CRAB_LOG("type-check", crab::outs () << "Type checking CFG ...\n";);
	
	// some sanity checks about the CFG
	if (m_cfg.size() == 0) 
	  CRAB_ERROR("CFG must have at least one basic block");
	if (!m_cfg.has_exit())
	  CRAB_ERROR("CFG must have exit block");
	if (m_cfg.size() == 1) {
	  if (!(m_cfg.exit() == m_cfg.entry()))
	    CRAB_ERROR("CFG entry and exit must be the same");
	}
	// check all statement are well typed
	type_checker_visitor vis;
	for (auto &b: boost::make_iterator_range(m_cfg.begin(), m_cfg.end())) {
	  b.accept(&vis);
	}
	
	CRAB_LOG("type-check", crab::outs () << "CFG is well-typed!\n";);
      }
      
     private:

      typedef typename CFG::varname_t V;
      typedef typename CFG::number_t N;

      CFG m_cfg;
      
      struct type_checker_visitor: public statement_visitor<N,V> {
	typedef typename statement_visitor<N,V>::bin_op_t bin_op_t;
	typedef typename statement_visitor<N,V>::assign_t assign_t;
	typedef typename statement_visitor<N,V>::assume_t assume_t;
	typedef typename statement_visitor<N,V>::assert_t assert_t;
	typedef typename statement_visitor<N,V>::int_cast_t int_cast_t;    
	typedef typename statement_visitor<N,V>::select_t select_t;    
	typedef typename statement_visitor<N,V>::havoc_t havoc_t;
	typedef typename statement_visitor<N,V>::unreach_t unreach_t;
	typedef typename statement_visitor<N,V>::callsite_t callsite_t;
	typedef typename statement_visitor<N,V>::return_t return_t;
	typedef typename statement_visitor<N,V>::arr_assume_t arr_assume_t;
	typedef typename statement_visitor<N,V>::arr_store_t arr_store_t;
	typedef typename statement_visitor<N,V>::arr_load_t arr_load_t;
	typedef typename statement_visitor<N,V>::arr_assign_t arr_assign_t;	
	typedef typename statement_visitor<N,V>::ptr_store_t ptr_store_t;
	typedef typename statement_visitor<N,V>::ptr_load_t ptr_load_t;
	typedef typename statement_visitor<N,V>::ptr_assign_t ptr_assign_t;
	typedef typename statement_visitor<N,V>::ptr_object_t ptr_object_t;
	typedef typename statement_visitor<N,V>::ptr_function_t ptr_function_t;
	typedef typename statement_visitor<N,V>::ptr_null_t ptr_null_t;
	typedef typename statement_visitor<N,V>::ptr_assume_t ptr_assume_t;
	typedef typename statement_visitor<N,V>::ptr_assert_t ptr_assert_t;
	typedef typename statement_visitor<N,V>::bool_bin_op_t bool_bin_op_t;
	typedef typename statement_visitor<N,V>::bool_assign_cst_t bool_assign_cst_t;
	typedef typename statement_visitor<N,V>::bool_assign_var_t bool_assign_var_t;    
	typedef typename statement_visitor<N,V>::bool_assume_t bool_assume_t;
	typedef typename statement_visitor<N,V>::bool_assert_t bool_assert_t;
	typedef typename statement_visitor<N,V>::bool_select_t bool_select_t;

	typedef typename CFG::statement_t statement_t;    

	typedef ikos::linear_expression<N,V> lin_exp_t;
	typedef ikos::linear_constraint<N,V> lin_cst_t;
	typedef ikos::variable<N,V> variable_t;
	typedef ikos::variable_ref<N,V> variable_ref_t;		
	
        type_checker_visitor () {}

	void check_num(variable_t v, std::string msg, statement_t& s) {
	  if (v.get_type() != INT_TYPE && v.get_type() != REAL_TYPE) {
	    crab::crab_string_os os;
	    os << "(type checking) " << msg << " in " << s;
	    CRAB_ERROR(os.str());
	  }
	}

	void check_int_or_bool(variable_t v, std::string msg, statement_t& s) {
	  if (v.get_type() != INT_TYPE && v.get_type() != BOOL_TYPE) {
	    crab::crab_string_os os;
	    os << "(type checking) " << msg << " in " << s;
	    CRAB_ERROR(os.str());
	  }
	}
	
	void check_int(variable_t v, std::string msg, statement_t& s) {
	  if ((v.get_type() != INT_TYPE) || (v.get_bitwidth() <= 1)) {
	    crab::crab_string_os os;
	    os << "(type checking) " << msg << " in " << s;
	    CRAB_ERROR(os.str());
	  }
	}

	void check_bool(variable_t v, std::string msg, statement_t& s) {
	  if ((v.get_type() != BOOL_TYPE) || (v.get_bitwidth() != 1)) {
	    crab::crab_string_os os;
	    os << "(type checking) " << msg << " in " << s;
	    CRAB_ERROR(os.str());
	  }
	}

	// static void check_ptr(variable_t v, std::string msg, statement_t& s) {
	//   if ((v.get_type() != PTR_TYPE)) {
	//     CRAB_ERROR("(type checking) ",msg," in ",s);
	//   }
	// }
	
	void check_bitwidth_if_int(variable_t v, std::string msg, statement_t& s) {
	  if (v.get_type() == INT_TYPE) {
	    if (v.get_bitwidth() <= 1) {
	      crab::crab_string_os os;
	      os << "(type checking) " << msg << " in " << s;
	      CRAB_ERROR(os.str());
	    }
	  }
	}

	void check_bitwidth_if_bool(variable_t v, std::string msg, statement_t& s) {
	  if (v.get_type() == BOOL_TYPE) {
	    if (v.get_bitwidth() != 1) {
	      crab::crab_string_os os;
	      os << "(type checking) " << msg << " in " << s;
	      CRAB_ERROR(os.str());
	    }
	  }
	}
	
	void check_same_type(variable_t v1, variable_t v2, std::string msg, statement_t& s) {
	  if (v1.get_type() != v2.get_type()) {
	    crab::crab_string_os os;
	    os << "(type checking) " << msg << " in " << s;
	    CRAB_ERROR(os.str());
	  }
	}

        void check_same_bitwidth(variable_t v1, variable_t v2, std::string msg, statement_t& s) {
	  // assume v1 and v2 have same type
	  if (v1.get_type() == INT_TYPE || v1.get_type() == BOOL_TYPE) {
	    if (v1.get_bitwidth() != v2.get_bitwidth()) {
	      crab::crab_string_os os;
	      os << "(type checking) " << msg << " in " << s;
	      CRAB_ERROR(os.str());
	    }
	  }
	}
	
        void visit(bin_op_t& s){
	  variable_t lhs = s.lhs(); 
	  lin_exp_t op1 = s.left();
	  lin_exp_t op2 = s.right();	  

	  check_num(lhs, "lhs must be integer or real", s);
	  check_bitwidth_if_int(lhs, "lhs must be have bitwidth > 1", s);
	  
	  if (boost::optional<variable_t> v1 = op1.get_variable()) {
	    check_same_type(lhs, *v1, "first operand cannot have different type from lhs", s);
	    check_same_bitwidth(lhs, *v1, "first operand cannot have different bitwidth from lhs", s);
	  } else {
	    CRAB_ERROR("(type checking) first binary operand must be a variable in ",s);
	  }  
	  if (boost::optional<variable_t> v2 = op2.get_variable()) {
	    check_same_type(lhs, *v2, "second operand cannot have different type from lhs", s);
	    check_same_bitwidth(lhs, *v2, "second operand cannot have different bitwidth from lhs", s);
	  } else {
	    // TODO: we can still check that we use z_number
	    // (q_number) of INT_TYPE (REAL_TYPE)
	  }
	}
	
        void visit(assign_t& s) {
	  variable_t lhs = s.lhs();
	  lin_exp_t rhs = s.rhs();

	  check_num(lhs, "lhs must be integer or real", s);
	  check_bitwidth_if_int(lhs, "lhs must be have bitwidth > 1", s);
	  
	  typename lin_exp_t::variable_set_t vars = rhs.variables();
	  for (auto const &v: vars) {
	    check_same_type(lhs, v, "variable cannot have different type from lhs", s);
	    check_same_bitwidth(lhs, v, "variable cannot have different bitwidth from lhs", s);
	  }
	}
	
        void visit(assume_t& s) {
	  typename lin_exp_t::variable_set_t vars = s.constraint().variables();
	  bool first = true;
	  variable_ref_t first_var;
	  for (auto const &v: vars) {
	    check_num(v, "assume variables must be integer or real", s);	    
	    if (first) {
	      first_var = variable_ref_t(v);
	      first = false;	      
	    }
	    check_same_type(first_var.get(), v, "inconsistent types in assume variables", s);
	    check_same_bitwidth(first_var.get(), v, "inconsistent bitwidths in assume variables", s);
	  }
	}
	
        void visit(assert_t& s) {
	  typename lin_exp_t::variable_set_t vars = s.constraint().variables();
	  bool first = true;
	  variable_ref_t first_var;
	  for (auto const &v: vars) {
	    check_num(v, "assert variables must be integer or real", s);	    
	    if (first) {
	      first_var = variable_ref_t(v);
	      first = false;	      
	    }
	    check_same_type(first_var.get(), v, "inconsistent types in assert variables", s);
	    check_same_bitwidth(first_var.get(), v, "inconsistent bitwidths in assert variables", s);
	  }
	}

	void visit(select_t& s){
	  check_num(s.lhs(), "lhs must be integer or real", s);
	  check_bitwidth_if_int(s.lhs(), "lhs must be have bitwidth > 1", s);	  
	  
	  typename lin_exp_t::variable_set_t left_vars = s.left().variables();
	  for (auto const &v: left_vars) {
	    check_same_type(s.lhs(), v, "inconsistent types in select variables", s);
	    check_same_bitwidth(s.lhs(), v, "inconsistent bitwidths in select variables", s);
	  }
	  typename lin_exp_t::variable_set_t right_vars = s.right().variables();
	  for (auto const &v: right_vars) {
	    check_same_type(s.lhs(), v, "inconsistent types in select variables", s);
	    check_same_bitwidth(s.lhs(), v, "inconsistent bitwidths in select variables", s);
	  }

	  // -- The condition can have different bitwidth from
	  //    lhs/left/right operands but must have same type.
	  typename lin_exp_t::variable_set_t cond_vars = s.cond().variables();
	  bool first = true;
	  variable_ref_t first_var;
	  for (auto const &v: cond_vars) {
	    check_num(v, "assume variables must be integer or real", s);	    
	    if (first) {
	      first_var = variable_ref_t(v);
	      first = false;	      
	    }
	    check_same_type(s.lhs(), v, "inconsistent types in select condition variables", s);	    	    
	    check_same_type(first_var.get(), v, "inconsistent types in select condition variables", s);
	    check_same_bitwidth(first_var.get(), v, "inconsistent bitwidths in select condition variables", s);
	  }
	}
	
        void visit(int_cast_t& s) {
	  variable_t src = s.src();
	  variable_t dst = s.dst();
	  switch (s.op()) {
	  case CAST_TRUNC:
	    check_int(src, "source operand must be integer", s);
	    check_int_or_bool(dst, "destination must be integer or bool", s);
	    check_bitwidth_if_bool(dst, "type and bitwidth of destination operand do not match", s);
	    check_bitwidth_if_int(dst, "type and bitwidth of destination operand do not match", s);	    
	    if (src.get_bitwidth() <= dst.get_bitwidth()) {
	      CRAB_ERROR("(type checking) bitwidth of source operand must be greater than destination in ",s);
	    }
	    break;
	  case CAST_SEXT:
	  case CAST_ZEXT:
	    check_int(dst, "destination operand must be integer", s);
	    check_int_or_bool(src, "source must be integer or bool", s);
	    check_bitwidth_if_bool(src, "type and bitwidth of source operand do not match", s);
	    check_bitwidth_if_int(src, "type and bitwidth of source operand do not match", s);	    
	    if (dst.get_bitwidth() <= src.get_bitwidth()) {
	      CRAB_ERROR("(type checking) bitwidth of destination must be greater than source in ",s);
	    }
	    break;
	  default:;; /*unreachable*/  
	  }
	}
	
        void visit(havoc_t &) {}
        void visit(unreach_t&){}

	void visit(bool_bin_op_t& s) {
	  check_bool(s.lhs(), "lhs must be boolean", s);
	  check_bool(s.left(), "first operand must be boolean", s);
	  check_bool(s.right(), "second operand must be boolean", s);	  
	};
	
	void visit(bool_assign_cst_t& s) {
	  check_bool(s.lhs(), "lhs must be boolean", s);

	  typename lin_exp_t::variable_set_t vars = s.rhs().variables();
	  bool first = true;
	  variable_ref_t first_var;
	  for (auto const &v: vars) {
	    check_num(v, "rhs variables must be integer or real", s);	    
	    if (first) {
	      first_var = variable_ref_t(v);
	      first = false;	      
	    }
	    check_same_type(first_var.get(), v, "inconsistent types in rhs variables", s);
	    check_same_bitwidth(first_var.get(), v, "inconsistent bitwidths in rhs variables", s);
	  }
	};
	
	void visit(bool_assign_var_t& s) {
	  check_bool(s.lhs(), "lhs must be boolean", s);
	  check_bool(s.rhs(), "rhs must be boolean", s);
	};
	
	void visit(bool_assume_t& s) {
	  check_bool(s.cond(), "condition must be boolean", s);
	};

	void visit(bool_assert_t& s) {
	  check_bool(s.cond(), "condition must be boolean", s);	  
	};
	
	void visit(bool_select_t& s) {
	  check_bool(s.lhs(), "lhs must be boolean", s);
	  check_bool(s.lhs(), "condition must be boolean", s);	  
	  check_bool(s.left(), "first operand must be boolean", s);
	  check_bool(s.right(), "second operand must be boolean", s);	  
	};
	
	/** TODO: type checking of the following statements: **/
	void visit(callsite_t&) {};
	void visit(return_t&) {};
	void visit(arr_assume_t&) {};
	void visit(arr_store_t&) {};
	void visit(arr_load_t&) {};
	void visit(arr_assign_t&) {};
	void visit(ptr_store_t&) {};
	void visit(ptr_load_t&) {};
	void visit(ptr_assign_t&) {};
	void visit(ptr_object_t&) {};
	void visit(ptr_function_t&) {};
	void visit(ptr_null_t&) {};
	void visit(ptr_assume_t&) {};
	void visit(ptr_assert_t&) {};
      }; // end class type_checker_visitor
    }; // end class type_checker
      
    // extending boost::hash for cfg class
    template<class B, class V, class N>
    std::size_t hash_value(Cfg<B,V,N> const& _cfg) {
      auto fdecl = _cfg.get_func_decl ();            
      if (!fdecl)
        CRAB_ERROR ("cannot hash a cfg because function declaration is missing");

      return cfg_hasher<Cfg<B,V,N> >::hash(*fdecl);
    }

    template<class CFG>
    std::size_t hash_value(cfg_ref<CFG> const& _cfg) {
      auto fdecl = _cfg.get().get_func_decl ();            
      if (!fdecl)
        CRAB_ERROR ("cannot hash a cfg because function declaration is missing");

      return cfg_hasher<cfg_ref<CFG> >::hash(*fdecl);
    }

    template<class CFGRef>
    std::size_t hash_value(cfg_rev<CFGRef> const& _cfg) {
      auto fdecl = _cfg.get().get_func_decl ();            
      if (!fdecl)
        CRAB_ERROR ("cannot hash a cfg because function declaration is missing");

      return cfg_hasher<cfg_rev<CFGRef> >::hash(*fdecl);
    }

    template<class B, class V, class N>
    bool operator==(Cfg<B,V,N> const& a, Cfg<B,V,N> const& b) {
      return hash_value (a) == hash_value (b);
    }

    template<class CFG>
    bool operator==(cfg_ref<CFG> const& a, cfg_ref<CFG> const& b) {
      return hash_value (a) == hash_value (b);
    }

    template<class CFGRef>
    bool operator==(cfg_rev<CFGRef> const& a, cfg_rev<CFGRef> const& b) {
      return hash_value (a) == hash_value (b);
    }

  } // end namespace cfg
}  // end namespace crab


