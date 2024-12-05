module github.com/whisthq/whist/backend/services

go 1.19

require (
	github.com/MicahParks/keyfunc v1.2.2
	github.com/NVIDIA/go-nvml v0.11.6-0
	github.com/aws/aws-sdk-go-v2 v1.16.11
	github.com/aws/aws-sdk-go-v2/config v1.17.1
	github.com/aws/aws-sdk-go-v2/service/ec2 v1.53.0
	github.com/bgentry/heroku-go v0.0.0-20150810151148-ee4032d686ae
	github.com/docker/docker v20.10.14+incompatible
	github.com/docker/go-connections v0.4.0
	github.com/docker/go-units v0.4.0
	github.com/fsnotify/fsnotify v1.5.4
	github.com/getsentry/sentry-go v0.13.0
	github.com/go-co-op/gocron v1.16.2
	github.com/golang-jwt/jwt/v4 v4.4.2
	github.com/google/uuid v1.3.0
	github.com/gorilla/websocket v1.5.0
	github.com/graph-gophers/graphql-go v1.3.0
	github.com/graph-gophers/graphql-transport-ws v0.0.2
	github.com/hashicorp/go-multierror v1.1.1
	github.com/hashicorp/go-version v1.6.0
	github.com/hasura/go-graphql-client v0.7.1
	github.com/jackc/pgconn v1.13.0
	github.com/jackc/pgtype v1.12.0
	github.com/jackc/pgx/v4 v4.17.0
	github.com/lithammer/shortuuid/v3 v3.0.7
	github.com/logzio/logzio-go v1.0.6
	github.com/opencontainers/image-spec v1.0.3-0.20211202183452-c5a74bcca799
	github.com/pierrec/lz4/v4 v4.1.15
	github.com/pkg/errors v0.9.1
	github.com/shirou/gopsutil/v3 v3.22.7
	github.com/stripe/stripe-go/v72 v72.122.0
	go.uber.org/zap v1.22.0
	golang.org/x/crypto v0.0.0-20220817201139-bc19a97f63c8
	golang.org/x/exp v0.0.0-20220915105810-2d61f44442a3
	golang.org/x/sync v0.0.0-20220819030929-7fc1605a5dde
	golang.org/x/time v0.0.0-20220722155302-e5dcc9cfc0b9
)

require (
	github.com/Microsoft/go-winio v0.5.2 // indirect
	github.com/aws/aws-sdk-go-v2/credentials v1.12.14 // indirect
	github.com/aws/aws-sdk-go-v2/feature/ec2/imds v1.12.12 // indirect
	github.com/aws/aws-sdk-go-v2/internal/configsources v1.1.18 // indirect
	github.com/aws/aws-sdk-go-v2/internal/endpoints/v2 v2.4.12 // indirect
	github.com/aws/aws-sdk-go-v2/internal/ini v1.3.19 // indirect
	github.com/aws/aws-sdk-go-v2/service/internal/presigned-url v1.9.12 // indirect
	github.com/aws/aws-sdk-go-v2/service/sso v1.11.17 // indirect
	github.com/aws/aws-sdk-go-v2/service/sts v1.16.13 // indirect
	github.com/aws/smithy-go v1.12.1 // indirect
	github.com/beeker1121/goque v2.1.0+incompatible // indirect
	github.com/bgentry/testnet v0.0.0-20131107221340-05450cdcf16c // indirect
	github.com/containerd/containerd v1.6.6 // indirect
	github.com/docker/distribution v2.8.1+incompatible // indirect
	github.com/go-ole/go-ole v1.2.6 // indirect
	github.com/gogo/protobuf v1.3.2 // indirect
	github.com/golang/protobuf v1.5.2 // indirect
	github.com/golang/snappy v0.0.4 // indirect
	github.com/gorilla/mux v1.8.0 // indirect
	github.com/hashicorp/errwrap v1.1.0 // indirect
	github.com/jackc/chunkreader/v2 v2.0.1 // indirect
	github.com/jackc/pgio v1.0.0 // indirect
	github.com/jackc/pgpassfile v1.0.0 // indirect
	github.com/jackc/pgproto3/v2 v2.3.1 // indirect
	github.com/jackc/pgservicefile v0.0.0-20200714003250-2b9c44734f2b // indirect
	github.com/jackc/puddle v1.2.1 // indirect
	github.com/jmespath/go-jmespath v0.4.0 // indirect
	github.com/klauspost/compress v1.15.1 // indirect
	github.com/lufia/plan9stats v0.0.0-20220326011226-f1430873d8db // indirect
	github.com/moby/term v0.0.0-20210619224110-3f7ff695adc6 // indirect
	github.com/morikuni/aec v1.0.0 // indirect
	github.com/onsi/ginkgo v1.16.4 // indirect
	github.com/onsi/gomega v1.17.0 // indirect
	github.com/opencontainers/go-digest v1.0.0 // indirect
	github.com/opentracing/opentracing-go v1.1.0 // indirect
	github.com/pborman/uuid v1.2.1 // indirect
	github.com/power-devops/perfstat v0.0.0-20220216144756-c35f1ee13d7c // indirect
	github.com/robfig/cron/v3 v3.0.1 // indirect
	github.com/sirupsen/logrus v1.8.1 // indirect
	github.com/syndtr/goleveldb v1.0.0 // indirect
	github.com/tklauser/go-sysconf v0.3.10 // indirect
	github.com/tklauser/numcpus v0.4.0 // indirect
	github.com/yusufpapurcu/wmi v1.2.2 // indirect
	go.uber.org/atomic v1.9.0 // indirect
	go.uber.org/goleak v1.1.12 // indirect
	go.uber.org/multierr v1.8.0 // indirect
	golang.org/x/net v0.0.0-20220325170049-de3da57026de // indirect
	golang.org/x/sys v0.0.0-20220722155257-8c9f86f7a55f // indirect
	golang.org/x/text v0.3.8 // indirect
	google.golang.org/genproto v0.0.0-20220328180837-c47567c462d1 // indirect
	google.golang.org/grpc v1.45.0 // indirect
	google.golang.org/protobuf v1.28.0 // indirect
	gotest.tools/v3 v3.1.0 // indirect
	nhooyr.io/websocket v1.8.7 // indirect
)

replace github.com/gin-gonic/gin => github.com/gin-gonic/gin v1.8.1
