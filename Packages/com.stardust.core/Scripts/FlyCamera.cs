using Stardust;
namespace Stardust.Engine;
// Included controller used by the default scene. RMB captures the mouse; WASD flies.
public class FlyCamera : ScriptBehaviour {
 public float Speed=8, Sensitivity=.15f;float yaw,pitch;
 void OnCreate(){var rotation=transform.localEulerAngles;yaw=rotation.y;pitch=rotation.x;}
 void OnUpdate(float dt){bool flying=Input.GetMouseButton(MouseButton.Right);Input.cursorLocked=flying;if(!flying)return;var d=Input.mouseDelta;yaw+=d.x*Sensitivity;pitch=Mathf.Clamp(pitch-d.y*Sensitivity,-89,89);transform.localEulerAngles=new Vector3(pitch,yaw,0);var direction=transform.right*Input.GetAxis("Horizontal")+transform.forward*Input.GetAxis("Vertical");transform.position+=direction*Speed*dt;}
}
