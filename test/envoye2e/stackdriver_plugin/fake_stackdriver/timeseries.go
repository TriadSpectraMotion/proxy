// Copyright 2019 Istio Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package fakestackdriver

// ServerRequestCountJSON is a JSON string of server request count metric protocol.
const ServerRequestCountJSON = `{
	"metric":{
	   "type":"istio.io/service/server/request_count",
	   "labels":{
		  "destination_owner":"kubernetes://api/apps/v1/namespaces/default/deployment/ratings-v1",
		  "destination_port":"20020",
		  "destination_principal":"",
		  "destination_service_name":"localhost:20016",
		  "destination_service_namespace":"default",
		  "destination_workload_name":"ratings-v1",
		  "destination_workload_namespace":"default",
		  "mesh_uid":"",
		  "request_operation":"GET",
		  "request_protocol":"http",
		  "response_code":"200",
		  "service_authentication_policy":"NONE",
		  "source_owner":"kubernetes://api/apps/v1/namespaces/default/deployment/productpage-v1",
		  "source_principal":"",
		  "source_workload_name":"productpage-v1",
		  "source_workload_namespace":"default"
	   }
	},
	"resource":{
	   "type":"k8s_container",
	   "labels":{
		  "cluster_name":"test-cluster",
		  "container_name":"istio-proxy",
		  "location":"us-east4-b",
		  "namespace_name":"default",
		  "pod_name":"ratings-v1-84975bc778-pxz2w",
		  "project_id":"test-project"
	   }
	},
	"points":[
	   {
		  "value":{
			 "int64Value":"10"
		  }
	   }
	]
 }`

// ClientRequestCountJSON is a JSON string of client request count metric protocol.
const ClientRequestCountJSON = `{
	"metric":{
	   "type":"istio.io/service/client/request_count",
	   "labels":{
		  "destination_owner":"kubernetes://api/apps/v1/namespaces/default/deployment/ratings-v1",
		  "destination_port":"20019",
		  "destination_principal":"",
		  "destination_service_name":"localhost:20016",
		  "destination_service_namespace":"default",
		  "destination_workload_name":"ratings-v1",
		  "destination_workload_namespace":"default",
		  "mesh_uid":"",
		  "request_operation":"GET",
		  "request_protocol":"http",
		  "response_code":"200",
		  "service_authentication_policy":"NONE",
		  "source_owner":"kubernetes://api/apps/v1/namespaces/default/deployment/productpage-v1",
		  "source_principal":"",
		  "source_workload_name":"productpage-v1",
		  "source_workload_namespace":"default"
	   }
	},
	"resource":{
	   "type":"k8s_pod",
	   "labels":{
		  "cluster_name":"test-cluster",
		  "location":"us-east4-b",
		  "namespace_name":"default",
		  "pod_name":"productpage-v1-84975bc778-pxz2w",
		  "project_id":"test-project"
	   }
	},
	"points":[
	   {
		  "value":{
			 "int64Value":"10"
		  }
	   }
	]
}`
