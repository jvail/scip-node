var scip_bindings = require('./build/Release/scip_node_bindings_async');

/*

  process only one lp at time

*/

var queue = (function () {

  var reqs = [];

  var add = function (req, lp, reqCb) {

    if (reqs.length > 100)
      return reqCb({ error: 'too many request queued' });
    else {

      var q = {
          lp: lp
        , reqCb: reqCb
        , closed: false  
      };
      
      req.on('close', function () { q.closed = true; });
      
      reqs.push(q);

      if (reqs.length === 1)
        next();

    }

  };

  var next = function () {

    if (reqs.length > 0) {

      if (!reqs[0].closed) {

        var count = reqs.length
          , reqCb = reqs[0].reqCb
          ;

        solveAsync(reqs[0].lp, function (data) {

          reqCb(data);
          reqs.shift();
          if (count > 0) next();

        });

      } else reqs.shift();

    }

  };

  return {
      add: add
  };

}());

var solveAsync = function (lp, queueCb) {

  scip_bindings.runAsync(lp.variables, lp.constraints, queueCb);

};

exports.addToQueue = queue.add;

//test
/*
var lp = {
    variables: {
        sEg1p1: 1
      , dEg1p1: 1
      , sPg1p1: 1
      , dPg1p1: 1
      , G1g1p1: 0
      , SIg1p1: 0
      , MSg1p1: 0
      , CCg1p1: 0
      
      , sEg1p2: 1
      , dEg1p2: 1
      , sPg1p2: 1
      , dPg1p2: 1
      , G2g1p2: 0
      , SIg1p2: 0
      , MSg1p2: 0
      , CCg1p2: 0

      , sEg2p1: 1
      , dEg2p1: 1
      , sPg2p1: 1
      , dPg2p1: 1
      , G1g2p1: 0
      , SIg2p1: 0
      , MSg2p1: 0
      , CCg2p1: 0

      , sEg2p2: 1
      , dEg2p2: 1
      , sPg2p2: 1
      , dPg2p2: 1
      , G2g2p2: 0
      , SIg2p2: 0
      , MSg2p2: 0
      , CCg2p2: 0
    }
  , constraints: [
        // group 1 period 1
        { name: 'Eg1p1', sEg1p1: -1, dEg1p1: 1, G1g1p1: 0.0361, SIg1p1: 0.0327, MSg1p1: 0.0370, CCg1p1: 0.0407, lhs: 1.0, rhs: 1.0 }
      , { name: 'Pg1p1', sPg1p1: -1, dPg1p1: 1, G1g1p1: 0.0358, SIg1p1: 0.0324, MSg1p1: 0.0326, CCg1p1: 0.0444, lhs: 1.0, rhs: 1.0 }
      , { name: 'DMg1p1', G1g1p1: 1.0081, SIg1p1: 1.0405, MSg1p1: 1.0000 , CCg1p1: 0.3905, lhs: 1.0, rhs: 15.7 }
        // group 1 period 2
      , { name: 'Eg1p2', sEg1p2: -1, dEg1p2: 1, G2g1p2: 0.0336, SIg1p2: 0.0327, MSg1p2: 0.0370, CCg1p2: 0.0407, lhs: 1.0, rhs: 1.0 }
      , { name: 'Pg1p2', sPg1p2: -1, dPg1p2: 1, G2g1p2: 0.0336, SIg1p2: 0.0324, MSg1p2: 0.0326, CCg1p2: 0.0444, lhs: 1.0, rhs: 1.0 }
      , { name: 'DMg1p2', G2g1p2: 1.0314, SIg1p2: 1.0405, MSg1p2: 1.0000 , CCg1p2: 0.3905, lhs: 1.0, rhs: 15.7 }
        // group 2 period 1
      , { name: 'Eg2p1', sEg2p1: -1, dEg2p1: 1, G1g2p1: 0.0646, SIg2p1: 0.0585, MSg2p1: 0.0662, CCg2p1: 0.0728, lhs: 1.0, rhs: 1.0 }
      , { name: 'Pg2p1', sPg2p1: -1, dPg2p1: 1, G1g2p1: 0.0624, SIg2p1: 0.0565, MSg2p1: 0.0570, CCg2p1: 0.0776, lhs: 1.0, rhs: 1.0 }
      , { name: 'DMg2p1', G1g2p1: 1.0088, SIg2p1: 1.0439, MSg2p1: 1.0000 , CCg2p1: 0.5502, lhs: 1.0, rhs: 14.5 }
        // group 2 period 2
      , { name: 'Eg2p2', sEg2p2: -1, dEg2p2: 1, G2g2p2: 0.0602, SIg2p2: 0.0585, MSg2p2: 0.0662, CCg2p2: 0.0728, lhs: 1.0, rhs: 1.0 }
      , { name: 'Pg2p2', sPg2p2: -1, dPg2p2: 1, G2g2p2: 0.0586, SIg2p2: 0.0565, MSg2p2: 0.0570, CCg2p2: 0.0776, lhs: 1.0, rhs: 1.0 }
      , { name: 'DMg2p2', G2g2p2: 1.0341, SIg2p2: 1.0439, MSg2p2: 1.0000 , CCg2p2: 0.5502, lhs: 1.0, rhs: 14.5 }
        // grazing constraints
      , { name: 'G1fixg1p1', G1g1p1: 1.0, lhs: 5.0, rhs: 5.0 }
      , { name: 'G2fixg1p2', G2g1p2: 1.0, lhs: 5.0, rhs: 5.0 }
      , { name: 'G1fixg2p1', G1g2p1: 1.0, lhs: 5.0, rhs: 5.0 }
      , { name: 'G2fixg2p2', G2g2p2: 1.0, lhs: 5.0, rhs: 5.0 }
        // concentrate constraints
      , { name: 'CCmaxg1p1', CCg1p1: 1.0, lhs: 0.0, rhs: 7.0 }
      , { name: 'CCmaxg1p2', CCg1p2: 1.0, lhs: 0.0, rhs: 7.0 }
      , { name: 'CCmaxg2p1', CCg2p1: 1.0, lhs: 0.0, rhs: 7.0 }
      , { name: 'CCmaxg2p2', CCg2p2: 1.0, lhs: 0.0, rhs: 7.0 }
        // maizesilage (total) constraint
      , { name: 'MSmax', MSg1p1: 1.0, MSg1p2: 1.0, MSg2p1: 1.0, MSg2p2: 1.0 , lhs: 0.0, rhs: 30.0 }
    ]
};  

scip.runAsync(lp.variables, lp.constraints, function (result) { console.log(result.objValue);});

for (var i = 1; i < 20; i++) {
  
  setTimeout(function () {
    console.log('tick');
  }, i * 1.5);

}
*/

