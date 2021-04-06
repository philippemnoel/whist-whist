// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"). You may
// not use this file except in compliance with the License. A copy of the
// License is located at
//
//     http://aws.amazon.com/apache2.0/
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.
//

// Code generated by MockGen. DO NOT EDIT.
// Source: github.com/fractal/fractal/ecs-host-service/ecsagent/agent/asm/factory (interfaces: ClientCreator)

// Package mock_factory is a generated GoMock package.
package mock_factory

import (
	reflect "reflect"

	secretsmanageriface "github.com/aws/aws-sdk-go/service/secretsmanager/secretsmanageriface"
	credentials "github.com/fractal/fractal/ecs-host-service/ecsagent/agent/credentials"
	gomock "github.com/golang/mock/gomock"
)

// MockClientCreator is a mock of ClientCreator interface
type MockClientCreator struct {
	ctrl     *gomock.Controller
	recorder *MockClientCreatorMockRecorder
}

// MockClientCreatorMockRecorder is the mock recorder for MockClientCreator
type MockClientCreatorMockRecorder struct {
	mock *MockClientCreator
}

// NewMockClientCreator creates a new mock instance
func NewMockClientCreator(ctrl *gomock.Controller) *MockClientCreator {
	mock := &MockClientCreator{ctrl: ctrl}
	mock.recorder = &MockClientCreatorMockRecorder{mock}
	return mock
}

// EXPECT returns an object that allows the caller to indicate expected use
func (m *MockClientCreator) EXPECT() *MockClientCreatorMockRecorder {
	return m.recorder
}

// NewASMClient mocks base method
func (m *MockClientCreator) NewASMClient(arg0 string, arg1 credentials.IAMRoleCredentials) secretsmanageriface.SecretsManagerAPI {
	m.ctrl.T.Helper()
	ret := m.ctrl.Call(m, "NewASMClient", arg0, arg1)
	ret0, _ := ret[0].(secretsmanageriface.SecretsManagerAPI)
	return ret0
}

// NewASMClient indicates an expected call of NewASMClient
func (mr *MockClientCreatorMockRecorder) NewASMClient(arg0, arg1 interface{}) *gomock.Call {
	mr.mock.ctrl.T.Helper()
	return mr.mock.ctrl.RecordCallWithMethodType(mr.mock, "NewASMClient", reflect.TypeOf((*MockClientCreator)(nil).NewASMClient), arg0, arg1)
}
