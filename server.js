var http = require('http')
  , scip = require('./scip.js')
  ; 

http.createServer(function (req, res) {

    if (req.method === 'POST') {

      console.log('request: ' + req.method);

      var body = [];
      req.on('data', function (chunk) { body.push(chunk); });
      req.on('end', function () {

        var lp = JSON.parse(body.join(''));
        /* cound not get async SCIP running properly */
        scip.addToQueue(req, lp, function (data) {

          res.writeHead(200, {'Content-Type': 'application/json'});
          res.end(JSON.stringify(data));
          console.log('solved');
  
        });

      });

    }

}).listen(8124, "127.0.0.1");

console.log('Server running at http://127.0.0.1:8124/');
