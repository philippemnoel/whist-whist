from sqlalchemy.orm import relationship

from ._meta import db


class MonitorLog(db.Model):
    __tablename__ = "monitor_logs"
    __table_args__ = {"extend_existing": True, "schema": "logs"}

    logons = db.Column(db.Integer)
    logoffs = db.Column(db.Integer)
    users_online = db.Column(db.Integer)
    eastus_available = db.Column(db.Integer)
    southcentralus_available = db.Column(db.Integer)
    northcentralus_available = db.Column(db.Integer)
    eastus_unavailable = db.Column(db.Integer)
    southcentralus_unavailable = db.Column(db.Integer)
    northcentralus_unavailable = db.Column(db.Integer)
    eastus_deallocated = db.Column(db.Integer)
    southcentralus_deallocated = db.Column(db.Integer)
    northcentralus_deallocated = db.Column(db.Integer)
    total_vms_deallocated = db.Column(db.Integer)
    timestamp = db.Column(db.Integer, nullable=False, primary_key=True)


class LoginHistory(db.Model):
    __tablename__ = "login_history"
    __table_args__ = {"extend_existing": True, "schema": "logs"}
    user_id = db.Column(db.ForeignKey("users.user_id"))
    user = relationship("User", back_populates="history")
    action = db.Column(db.String)
    timestamp = db.Column(db.Integer, primary_key=True)
