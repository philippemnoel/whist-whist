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
// Source: github.com/fractal/fractal/ecs-host-service/ecsagent/agent/eni/udevwrapper (interfaces: Udev)

// Package mock_udevwrapper is a generated GoMock package.
package mock_udevwrapper

import (
	reflect "reflect"

	udev "github.com/deniswernert/udev"
	gomock "github.com/golang/mock/gomock"
)

// MockUdev is a mock of Udev interface
type MockUdev struct {
	ctrl     *gomock.Controller
	recorder *MockUdevMockRecorder
}

// MockUdevMockRecorder is the mock recorder for MockUdev
type MockUdevMockRecorder struct {
	mock *MockUdev
}

// NewMockUdev creates a new mock instance
func NewMockUdev(ctrl *gomock.Controller) *MockUdev {
	mock := &MockUdev{ctrl: ctrl}
	mock.recorder = &MockUdevMockRecorder{mock}
	return mock
}

// EXPECT returns an object that allows the caller to indicate expected use
func (m *MockUdev) EXPECT() *MockUdevMockRecorder {
	return m.recorder
}

// Close mocks base method
func (m *MockUdev) Close() error {
	m.ctrl.T.Helper()
	ret := m.ctrl.Call(m, "Close")
	ret0, _ := ret[0].(error)
	return ret0
}

// Close indicates an expected call of Close
func (mr *MockUdevMockRecorder) Close() *gomock.Call {
	mr.mock.ctrl.T.Helper()
	return mr.mock.ctrl.RecordCallWithMethodType(mr.mock, "Close", reflect.TypeOf((*MockUdev)(nil).Close))
}

// Monitor mocks base method
func (m *MockUdev) Monitor(arg0 chan *udev.UEvent) chan bool {
	m.ctrl.T.Helper()
	ret := m.ctrl.Call(m, "Monitor", arg0)
	ret0, _ := ret[0].(chan bool)
	return ret0
}

// Monitor indicates an expected call of Monitor
func (mr *MockUdevMockRecorder) Monitor(arg0 interface{}) *gomock.Call {
	mr.mock.ctrl.T.Helper()
	return mr.mock.ctrl.RecordCallWithMethodType(mr.mock, "Monitor", reflect.TypeOf((*MockUdev)(nil).Monitor), arg0)
}
