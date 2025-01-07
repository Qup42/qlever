# Running public QLever instances

When running a public QLever instance, you should
- choose a secure access token
- disable federated queries (`SERVICE`)  

You can also 
- disabling updates completely

## Access token
An access token can be set to restrict access to certain operations like clearing the cache completely or listing all running queries.
All Updates operations and removing the changes from Updates also require the access token.
There is one access token per running QLever instance.
The access token can be set with the `-a` flag or with `ACCESS_TOKEN` under the `server` section in the `Qleverfile`.

## Federated queries
[Federated Queries](https://www.w3.org/TR/2013/REC-sparql11-federated-query-20130321/) can be run in queries by using the `SERVICE` keyword.
This can be used to execute queries against other SPARQL endpoints and use the results in the current query.
Federated queries can be disabled with the `-disable-federated-query` flag.

## Updates
Updates can only be executed with the access token.
Updates can be disabled completely with the `-disable-update` flag.