{
  'targets': [
    {
      'target_name': 'scip_node_bindings',
      'include_dirs': [
        '../../scipoptsuite-3.0.1/scip-3.0.1/src/scip',
        '../../scipoptsuite-3.0.1/scip-3.0.1/src/'
      ],
      'ldflags': [
        '-lscipopt-3.0.1.linux.x86_64.gnu.opt',
        '-L../../scipoptsuite-3.0.1/lib'
      ],
      'sources': [
        'scip_node_bindings.cc'
      ]
    }
  ]
}
