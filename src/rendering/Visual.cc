/*
 * Copyright 2011 Nate Koenig & Andrew Howard
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
/* Desc: Ogre Visual Class
 * Author: Nate Koenig
 * Date: 14 Dec 2007
 */

#include "rendering/ogre.h"
#include "sdf/sdf_parser.h"

#include "msgs/msgs.h"
#include "common/Events.hh"

#include "rendering/Conversions.hh"
#include "rendering/DynamicLines.hh"
#include "rendering/Scene.hh"
#include "rendering/SelectionObj.hh"
#include "rendering/RTShaderSystem.hh"
#include "common/MeshManager.hh"
#include "common/Console.hh"
#include "common/Exception.hh"
#include "common/Global.hh"
#include "common/Mesh.hh"
#include "rendering/Material.hh"
#include "rendering/Visual.hh"

using namespace gazebo;
using namespace rendering;


SelectionObj *Visual::selectionObj = 0;
unsigned int Visual::visualCounter = 0;

////////////////////////////////////////////////////////////////////////////////
// Constructor
Visual::Visual(const std::string &_name, VisualPtr _parent)
{
  this->SetName(_name);
  this->sceneNode = NULL;

  Ogre::SceneNode *pnode = NULL;
  if (_parent)
    pnode = _parent->GetSceneNode();
  else
    gzerr << "Create a visual, invalid parent!!!\n";

  std::string uniqueName = this->GetName();
  int index = 0;
  while (pnode->getCreator()->hasSceneNode( uniqueName ))
    uniqueName = this->GetName() + "_" + boost::lexical_cast<std::string>(index++);

  this->SetName(uniqueName);

  this->sceneNode = pnode->createChildSceneNode( this->GetName() );

  this->parent = _parent;
  this->Init();
}

////////////////////////////////////////////////////////////////////////////////
/// Constructor
Visual::Visual (const std::string &_name, Ogre::SceneNode *_parent)
{
  this->SetName(_name);
  this->sceneNode = NULL;

  std::string uniqueName = this->GetName();
  int index = 0;
  while (_parent->getCreator()->hasSceneNode( uniqueName ))
    uniqueName = this->GetName() + "_" + boost::lexical_cast<std::string>(index++);
  this->SetName(uniqueName);

  this->sceneNode = _parent->createChildSceneNode( this->GetName() );

  this->Init();
}

////////////////////////////////////////////////////////////////////////////////
/// Constructor
Visual::Visual (const std::string &_name, Scene *_scene)
{
  this->SetName(_name);
  this->sceneNode = NULL;

  std::string uniqueName = this->GetName();
  int index = 0;
  while (_scene->GetManager()->hasSceneNode( uniqueName ))
  {
    uniqueName = this->GetName() + "_" + boost::lexical_cast<std::string>(index++);
  }


  this->SetName(uniqueName);
  this->sceneNode = _scene->GetManager()->getRootSceneNode()->createChildSceneNode(this->GetName());

  this->Init();
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor
Visual::~Visual()
{
  if (this->preRenderConnection)
    event::Events::DisconnectPreRenderSignal( this->preRenderConnection );

  // delete instance from lines vector
  for (std::list<DynamicLines*>::iterator iter=this->lines.begin();
       iter!=this->lines.end();iter++)
    delete *iter;
  this->lines.clear();

  RTShaderSystem::Instance()->DetachEntity(this);

  if (this->sceneNode != NULL)
  {
    this->sceneNode->removeAllChildren();
    this->sceneNode->detachAllObjects();

    if (this->sceneNode->getParentSceneNode())
      this->sceneNode->getParentSceneNode()->removeAndDestroyChild( this->sceneNode->getName() );
    this->sceneNode = NULL;
  }

  this->parent.reset();
}

////////////////////////////////////////////////////////////////////////////////
// Helper for the contructor
void Visual::Init()
{
  this->sdf.reset(new sdf::Element);
  sdf::initFile( "/sdf/visual.sdf", this->sdf );

  this->transparency = 0.0;
  this->isStatic = false;
  this->visible = true;
  this->ribbonTrail = NULL;
  this->staticGeom = NULL;

  RTShaderSystem::Instance()->AttachEntity(this);
}

////////////////////////////////////////////////////////////////////////////////
void Visual::LoadFromMsg(const boost::shared_ptr< msgs::Visual const> &msg)
{
  sdf::ElementPtr geomElem = this->sdf->GetOrCreateElement("geometry");
  geomElem->ClearElements();

  if (msg->mesh_type() == msgs::Visual::BOX)
  {
    sdf::ElementPtr elem = geomElem->AddElement("box");
    elem->GetAttribute("size")->Set(msgs::Convert(msg->scale()));
  }
  else if (msg->mesh_type() == msgs::Visual::SPHERE)
  {
    sdf::ElementPtr elem = geomElem->AddElement("sphere");
    elem->GetAttribute("radius")->Set(msg->scale().x());
  }
  else if (msg->mesh_type() == msgs::Visual::CYLINDER)
  {
    sdf::ElementPtr elem = geomElem->AddElement("cylinder");
    elem->GetAttribute("radius")->Set(msg->scale().x());
    elem->GetAttribute("length")->Set(msg->scale().y());
  }
  else if (msg->mesh_type() == msgs::Visual::PLANE)
  {
    math::Plane plane = msgs::Convert(msg->plane());
    sdf::ElementPtr elem = geomElem->AddElement("plane");
    elem->GetAttribute("normal")->Set(plane.normal);
  }
  else if (msg->mesh_type() == msgs::Visual::MESH)
  {
    sdf::ElementPtr elem = geomElem->AddElement("mesh");
    elem->GetAttribute("filename")->Set( msg->filename() );
  }

  if (msg->has_pose())
  {
    sdf::ElementPtr elem = this->sdf->GetOrCreateElement("origin");
    math::Pose p( msgs::Convert(msg->pose().position()),
                  msgs::Convert(msg->pose().orientation()) );

    elem->GetAttribute("pose")->Set( p );
  }

  if (msg->has_material_script())
  {
    sdf::ElementPtr elem = this->sdf->GetOrCreateElement("material");
    elem->GetAttribute("script")->Set(msg->material_script());
  }

  if (msg->has_material_color())
  {
    sdf::ElementPtr elem = this->sdf->GetOrCreateElement("material");
    elem->GetOrCreateElement("color")->GetAttribute("rgba")->Set(
        msgs::Convert(msg->material_color()));
  }

  if (msg->has_cast_shadows())
  {
    this->sdf->GetAttribute("cast_shadows")->Set(msg->cast_shadows());
  }

  if (msg->has_scale())
    this->SetScale( msgs::Convert(msg->scale()) );

  this->Load();
  this->UpdateFromMsg(msg);
}

////////////////////////////////////////////////////////////////////////////////
// Load the visual
void Visual::Load( sdf::ElementPtr &_sdf)
{
  this->sdf = _sdf;
  this->Load();
}

////////////////////////////////////////////////////////////////////////////////
// Load the visual
void Visual::Load()
{
  std::ostringstream stream;
  math::Pose pose;
  Ogre::Vector3 meshSize(1,1,1);
  Ogre::MovableObject *obj = NULL;

  // Read the desired position and rotation of the mesh
  pose = this->sdf->GetOrCreateElement("origin")->GetValuePose("pose");

  std::string meshName = this->GetMeshName();

  if (!meshName.empty())
  {
    try
    {
      // Create the visual
      stream << "VISUAL_" << this->sceneNode->getName();

      if (!common::MeshManager::Instance()->HasMesh(meshName))
      {
        common::MeshManager::Instance()->Load(meshName);
      }

      // Add the mesh into OGRE
      this->InsertMesh( common::MeshManager::Instance()->GetMesh(meshName) );

      Ogre::SceneManager *mgr = this->sceneNode->getCreator();
      if (mgr->hasEntity(stream.str()))
        obj = (Ogre::MovableObject*)mgr->getEntity(stream.str());
      else
        obj = (Ogre::MovableObject*)mgr->createEntity( stream.str(), meshName);
    }
    catch (Ogre::Exception e)
    {
      gzerr << "Ogre Error:" << e.getFullDescription() << "\n";
      gzthrow("Unable to create a mesh from " + meshName);
    }
  }

  // Attach the entity to the node
  if (obj)
  {
    this->AttachObject(obj);
    obj->setVisibilityFlags(GZ_ALL_CAMERA);
  }

  // Set the pose of the scene node
  this->SetPose(pose);

  // Get the size of the mesh
  if (obj)
  {
    meshSize = obj->getBoundingBox().getSize();
  }
 
  math::Vector3 scale = this->GetScale(); 
  this->sceneNode->setScale( scale.x, scale.y, scale.z );

  // Set the material of the mesh
  if (this->sdf->HasElement("material"))
  {
    sdf::ElementPtr matElem = this->sdf->GetElement("material");
    std::string script = matElem->GetValueString("script");
    if (!script.empty())
      this->SetMaterial(script);
    else if (matElem->HasElement("color"))
    {
      this->SetColor( matElem->GetElement("color")->GetValueColor("rgba") );
    }
  }

  // Allow the mesh to cast shadows
  this->SetCastShadows(true);//this->sdf->GetValueBool("cast_shadows"));
}

////////////////////////////////////////////////////////////////////////////////
/// Update the visual.
void Visual::Update()
{
  if (!this->visible)
    return;

  std::list<DynamicLines*>::iterator iter;

  // Update the lines
  for (iter = this->lines.begin(); iter != this->lines.end(); iter++)
    (*iter)->Update();
}

////////////////////////////////////////////////////////////////////////////////
/// Set the name of the visual
void Visual::SetName( const std::string &name_ )
{
  this->name = name_;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the name of the visual
std::string Visual::GetName() const
{
  return this->name;
}

////////////////////////////////////////////////////////////////////////////////
/// Attach a visual
void Visual::AttachVisual(Visual *vis)
{
  if (!vis)
    gzerr << "Visual is null\n";
  else
  {
    if (vis->GetSceneNode()->getParentSceneNode())
    {
      vis->GetSceneNode()->getParentSceneNode()->removeChild(vis->GetSceneNode());
    }
    this->sceneNode->addChild( vis->GetSceneNode() );
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Detach a visual 
void Visual::DetachVisual(Visual *vis)
{
  this->sceneNode->removeChild(vis->GetSceneNode());
}

////////////////////////////////////////////////////////////////////////////////
/// Attach a renerable object to the visual
void Visual::AttachObject( Ogre::MovableObject *_obj)
{
  // This code makes plane render before grids. This allows grids to overlay
  // planes, and then other elements to overlay both planes and grids.
  if (this->sdf->HasElement("geometry"))
    if (this->sdf->GetElement("geometry")->HasElement("plane"))
      _obj->setRenderQueueGroup(Ogre::RENDER_QUEUE_WORLD_GEOMETRY_1 - 2);

  this->sceneNode->attachObject(_obj);
  //RTShaderSystem::Instance()->UpdateShaders();

  _obj->setUserAny( Ogre::Any(this->GetName()) );
}

////////////////////////////////////////////////////////////////////////////////
/// Detach all objects
void Visual::DetachObjects()
{
  this->sceneNode->detachAllObjects();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the number of attached objects
unsigned short Visual::GetNumAttached()
{
  return this->sceneNode->numAttachedObjects();
}

////////////////////////////////////////////////////////////////////////////////
/// Get an attached object
Ogre::MovableObject *Visual::GetAttached(unsigned short num)
{
  return this->sceneNode->getAttachedObject(num);
}

////////////////////////////////////////////////////////////////////////////////
// Attach a static object
void Visual::MakeStatic()
{
  /*if (!this->staticGeom)
    this->staticGeom = this->sceneNode->getCreator()->createStaticGeometry(this->sceneNode->getName() + "_Static");

  // Add the scene node to the static geometry
  this->staticGeom->addSceneNode(this->sceneNode);

  // Build the static geometry
  this->staticGeom->build();

  // Prevent double rendering
  this->sceneNode->setVisible(false);
  this->sceneNode->detachAllObjects();
  */
}

////////////////////////////////////////////////////////////////////////////////
/// Attach a mesh to this visual by name
void Visual::AttachMesh( const std::string &meshName )
{
  std::ostringstream stream;
  Ogre::MovableObject *obj;
  stream << this->sceneNode->getName() << "_ENTITY_" << meshName;

  // Add the mesh into OGRE
  if (!this->sceneNode->getCreator()->hasEntity(meshName) &&
      common::MeshManager::Instance()->HasMesh(meshName))
  {
    const common::Mesh *mesh = common::MeshManager::Instance()->GetMesh(meshName);

    this->InsertMesh( mesh );
  }

  obj = (Ogre::MovableObject*)(this->sceneNode->getCreator()->createEntity(stream.str(), meshName));

  this->AttachObject( obj );
}

////////////////////////////////////////////////////////////////////////////////
///  Set the scale
void Visual::SetScale(const math::Vector3 &scale )
{
  sdf::ElementPtr geomElem = this->sdf->GetOrCreateElement("geometry");

  if (geomElem->HasElement("box"))
    geomElem->GetElement("box")->GetAttribute("size")->Set(scale);
  else if (geomElem->HasElement("sphere"))
    geomElem->GetElement("sphere")->GetAttribute("radius")->Set(scale.x);
  else if (geomElem->HasElement("cylinder"))
  {
    geomElem->GetElement("cylinder")->GetAttribute("radius")->Set(scale.x);
    geomElem->GetElement("cylinder")->GetAttribute("length")->Set(scale.y);
  }
  else if (geomElem->HasElement("mesh"))
    geomElem->GetElement("mesh")->GetAttribute("scale")->Set(scale);

  this->sceneNode->setScale(Conversions::Convert(scale));
}

////////////////////////////////////////////////////////////////////////////////
/// Get the scale
math::Vector3 Visual::GetScale()
{
  math::Vector3 result(1,1,1);
  if (this->sdf->HasElement("geometry"))
  {
    sdf::ElementPtr geomElem = this->sdf->GetElement("geometry");

    if (geomElem->HasElement("box"))
      result = geomElem->GetElement("box")->GetValueVector3("size");
    else if (geomElem->HasElement("sphere"))
    {
      double r = geomElem->GetElement("sphere")->GetValueDouble("radius");
      result.Set(r,r,r);
    }
    else if (geomElem->HasElement("cylinder"))
    {
      double r = geomElem->GetElement("cylinder")->GetValueDouble("radius");
      double l = geomElem->GetElement("cylinder")->GetValueDouble("length");
      result.Set(r,r,l);
    }
    else if (geomElem->HasElement("plane"))
      result.Set(1,1,1);
    else if (geomElem->HasElement("mesh"))
      result = geomElem->GetElement("mesh")->GetValueVector3("scale");
  }

  return result;
}


////////////////////////////////////////////////////////////////////////////////
// Set the material
void Visual::SetMaterial(const std::string &materialName)
{
  if (materialName.empty())
    return;

  // Create a custom material name
  std::string newMaterialName;
  newMaterialName = this->sceneNode->getName() + "_MATERIAL_" + materialName;

  if (this->GetMaterialName() == newMaterialName)
    return;

  this->myMaterialName = newMaterialName;

  Ogre::MaterialPtr origMaterial;

  try
  {
    this->origMaterialName = materialName;
    // Get the original material
    origMaterial= Ogre::MaterialManager::getSingleton().getByName (materialName);;
  }
  catch (Ogre::Exception e)
  {
    gzwarn << "Unable to get Material[" << materialName << "] for Geometry["
    << this->sceneNode->getName() << ". Object will appear white.\n";
    return;
  }

  if (origMaterial.isNull())
  {
    gzwarn << "Unable to get Material[" << materialName << "] for Geometry["
    << this->sceneNode->getName() << ". Object will appear white\n";
    return;
  }


  Ogre::MaterialPtr myMaterial;

  // Clone the material. This will allow us to change the look of each geom
  // individually.
  if (Ogre::MaterialManager::getSingleton().resourceExists(this->myMaterialName))
  {
    myMaterial = (Ogre::MaterialPtr)(Ogre::MaterialManager::getSingleton().getByName(this->myMaterialName));
  }
  else
  {
    myMaterial = origMaterial->clone(this->myMaterialName);
  }


  try
  {
    for (int i=0; i < this->sceneNode->numAttachedObjects(); i++)
    {
      Ogre::MovableObject *obj = this->sceneNode->getAttachedObject(i);

      if (dynamic_cast<Ogre::Entity*>(obj))
        ((Ogre::Entity*)obj)->setMaterialName(this->myMaterialName);
      else
        ((Ogre::SimpleRenderable*)obj)->setMaterial(this->myMaterialName);
    }
  }
  catch (Ogre::Exception e)
  {
    gzwarn << "Unable to set Material[" << this->myMaterialName << "] to Geometry["
    << this->sceneNode->getName() << ". Object will appear white.\n";
  }

  RTShaderSystem::Instance()->UpdateShaders();
}

/// Set the color of the visual
void Visual::SetColor(const common::Color &/*_color*/)
{
  // TODO: implement this 
}

void Visual::AttachAxes()
{
  std::ostringstream nodeName;

  nodeName << this->sceneNode->getName()<<"_AXES_NODE";
 
  if (!this->sceneNode->getCreator()->hasEntity("axis_cylinder"))
    this->InsertMesh(common::MeshManager::Instance()->GetMesh("axis_cylinder"));

  Ogre::SceneNode *node = this->sceneNode->createChildSceneNode(nodeName.str());
  Ogre::SceneNode *x, *y, *z;

  x = node->createChildSceneNode(nodeName.str() + "_axisX");
  x->setInheritScale(true);
  x->translate(.25,0,0);
  x->yaw(Ogre::Radian(M_PI/2.0));

  y = node->createChildSceneNode(nodeName.str() + "_axisY");
  y->setInheritScale(true);
  y->translate(0,.25,0);
  y->pitch(Ogre::Radian(M_PI/2.0));

  z = node->createChildSceneNode(nodeName.str() + "_axisZ");
  z->translate(0,0,.25);
  z->setInheritScale(true);
  
  Ogre::MovableObject *xobj, *yobj, *zobj;

  xobj = (Ogre::MovableObject*)(node->getCreator()->createEntity(nodeName.str()+"X_AXIS", "axis_cylinder"));
  xobj->setCastShadows(false);
  ((Ogre::Entity*)xobj)->setMaterialName("Gazebo/Red");

  yobj = (Ogre::MovableObject*)(node->getCreator()->createEntity(nodeName.str()+"Y_AXIS", "axis_cylinder"));
  yobj->setCastShadows(false);
  ((Ogre::Entity*)yobj)->setMaterialName("Gazebo/Green");

  zobj = (Ogre::MovableObject*)(node->getCreator()->createEntity(nodeName.str()+"Z_AXIS", "axis_cylinder"));
  zobj->setCastShadows(false);
  ((Ogre::Entity*)zobj)->setMaterialName("Gazebo/Blue");

  x->attachObject(xobj);
  y->attachObject(yobj);
  z->attachObject(zobj);
}


////////////////////////////////////////////////////////////////////////////////
/// Set the transparency
void Visual::SetTransparency( float trans )
{
  this->transparency = std::min(std::max(trans, (float)0.0), (float)1.0);
  for (unsigned int i=0; i < this->sceneNode->numAttachedObjects(); i++)
  {
    Ogre::Entity *entity = NULL;
    Ogre::MovableObject *obj = this->sceneNode->getAttachedObject(i);

    entity = dynamic_cast<Ogre::Entity*>(obj);

    if (!entity)
      continue;

    // For each ogre::entity
    for (unsigned int j=0; j < entity->getNumSubEntities(); j++)
    {
      Ogre::SubEntity *subEntity = entity->getSubEntity(j);
      Ogre::MaterialPtr material = subEntity->getMaterial();
      Ogre::Material::TechniqueIterator techniqueIt = material->getTechniqueIterator();

      unsigned int techniqueCount, passCount;
      Ogre::Technique *technique;
      Ogre::Pass *pass;
      Ogre::ColourValue dc;

      for (techniqueCount = 0; techniqueCount < material->getNumTechniques(); 
           techniqueCount++)
      {
        technique = material->getTechnique(techniqueCount);

        for (passCount=0; passCount < technique->getNumPasses(); passCount++)
        {
          pass = technique->getPass(passCount);
          // Need to fix transparency
          if (!pass->isProgrammable() &&
              pass->getPolygonMode() == Ogre::PM_SOLID)
          {
            pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
          }

          if (this->transparency > 0.0)
            pass->setDepthWriteEnabled(false);
          else
            pass->setDepthWriteEnabled(true);

          dc = pass->getDiffuse();
          dc.a = (1.0f - this->transparency);
          pass->setDiffuse(dc);
        }
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Set the emissive value
void Visual::SetEmissive( const common::Color &_color )
{
  for (unsigned int i=0; i < this->sceneNode->numAttachedObjects(); i++)
  {
    Ogre::Entity *entity = NULL;
    Ogre::MovableObject *obj = this->sceneNode->getAttachedObject(i);

    entity = dynamic_cast<Ogre::Entity*>(obj);

    if (!entity)
      continue;

    // For each ogre::entity
    for (unsigned int j=0; j < entity->getNumSubEntities(); j++)
    {
      Ogre::SubEntity *subEntity = entity->getSubEntity(j);
      Ogre::MaterialPtr material = subEntity->getMaterial();
      Ogre::Material::TechniqueIterator techniqueIt = material->getTechniqueIterator();

      unsigned int techniqueCount, passCount;
      Ogre::Technique *technique;
      Ogre::Pass *pass;
      Ogre::ColourValue dc;

      for (techniqueCount = 0; techniqueCount < material->getNumTechniques(); 
           techniqueCount++)
      {
        technique = material->getTechnique(techniqueCount);

        for (passCount=0; passCount < technique->getNumPasses(); passCount++)
        {
          pass = technique->getPass(passCount);
          pass->setSelfIllumination( Conversions::Convert(_color) );
        }
      }
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
/// Get the transparency
float Visual::GetTransparency()
{
  return this->transparency;
}

////////////////////////////////////////////////////////////////////////////////
/// Set whether the visual should cast shadows
void Visual::SetCastShadows(const bool &shadows)
{
  for (int i=0; i < this->sceneNode->numAttachedObjects(); i++)
  {
    Ogre::MovableObject *obj = this->sceneNode->getAttachedObject(i);
    obj->setCastShadows(shadows);
  }

  if (this->IsStatic() && this->staticGeom)
    this->staticGeom->setCastShadows(shadows);
}

////////////////////////////////////////////////////////////////////////////////
/// Set whether the visual is visible
void Visual::SetVisible(bool visible_, bool cascade_)
{
  this->sceneNode->setVisible( visible_, cascade_ );
  this->visible = visible_;
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle whether this visual is visible
void Visual::ToggleVisible()
{
  this->SetVisible( !this->GetVisible() );
}

////////////////////////////////////////////////////////////////////////////////
/// Get whether the visual is visible
bool Visual::GetVisible() const
{
  return this->visible;
}

////////////////////////////////////////////////////////////////////////////////
// Set the position of the visual
void Visual::SetPosition( const math::Vector3 &_pos)
{
  /*if (this->IsStatic() && this->staticGeom)
  {
    this->staticGeom->reset();
    delete this->staticGeom;
    this->staticGeom = NULL;
    //this->staticGeom->setOrigin( Ogre::Vector3(pos.x, pos.y, pos.z) );
  }*/

  this->sceneNode->setPosition(_pos.x, _pos.y, _pos.z);

  std::list< std::pair<DynamicLines*, unsigned int> >::iterator iter;
  for (iter = this->lineVertices.begin(); iter != this->lineVertices.end(); iter++)
  {
    iter->first->SetPoint( iter->second, 
        Conversions::Convert(this->sceneNode->_getDerivedPosition()) );
    iter->first->Update();
  }

}


////////////////////////////////////////////////////////////////////////////////
// Set the rotation of the visual
void Visual::SetRotation( const math::Quaternion &rot)
{
  this->sceneNode->setOrientation(rot.w, rot.x, rot.y, rot.z);
}

////////////////////////////////////////////////////////////////////////////////
// Set the pose of the visual
void Visual::SetPose( const math::Pose &_pose)
{
  this->SetPosition( _pose.pos );
  this->SetRotation( _pose.rot);
}

////////////////////////////////////////////////////////////////////////////////
// Set the position of the visual
math::Vector3 Visual::GetPosition() const
{
  return Conversions::Convert(this->sceneNode->getPosition());
}

////////////////////////////////////////////////////////////////////////////////
// Get the rotation of the visual
math::Quaternion Visual::GetRotation( ) const
{
  return Conversions::Convert(this->sceneNode->getOrientation());
}

////////////////////////////////////////////////////////////////////////////////
// Get the pose of the visual
math::Pose Visual::GetPose() const
{
  math::Pose pos;
  pos.pos=this->GetPosition();
  pos.rot=this->GetRotation();
  return pos;
}

void Visual::SetWorldPose(const math::Pose _pose)
{
  Ogre::Vector3 vpos;
  Ogre::Quaternion vquatern;

  vpos.x = _pose.pos.x;
  vpos.y = _pose.pos.y;
  vpos.z = _pose.pos.z;

  vquatern.w = _pose.rot.w;
  vquatern.x = _pose.rot.x;
  vquatern.y = _pose.rot.y;
  vquatern.z = _pose.rot.z;

  this->sceneNode->_setDerivedPosition( vpos );
  this->sceneNode->_setDerivedOrientation( vquatern );
}

////////////////////////////////////////////////////////////////////////////////
/// Get the global pose of the node
math::Pose Visual::GetWorldPose() const
{
  math::Pose pose;

  Ogre::Vector3 vpos;
  Ogre::Quaternion vquatern;

  vpos=this->sceneNode->_getDerivedPosition();
  pose.pos.x=vpos.x;
  pose.pos.y=vpos.y;
  pose.pos.z=vpos.z;

  vquatern=this->sceneNode->getOrientation();
  pose.rot.w = vquatern.w;
  pose.rot.x = vquatern.x;
  pose.rot.y = vquatern.y;
  pose.rot.z = vquatern.z;


  return pose;
}


////////////////////////////////////////////////////////////////////////////////
// Get this visual Ogre node
Ogre::SceneNode * Visual::GetSceneNode() const
{
  return this->sceneNode;
}


////////////////////////////////////////////////////////////////////////////////
/// Return true if the  visual is a static geometry
bool Visual::IsStatic() const
{
  return this->isStatic;
}


////////////////////////////////////////////////////////////////////////////////
/// Set one visual to track/follow another
void Visual::EnableTrackVisual( Visual *vis )
{
  this->sceneNode->setAutoTracking(true, vis->GetSceneNode() );
}

////////////////////////////////////////////////////////////////////////////////
/// Disable tracking of a visual
void Visual::DisableTrackVisual()
{
  this->sceneNode->setAutoTracking(false);
}

////////////////////////////////////////////////////////////////////////////////
/// Get the normal map
std::string Visual::GetNormalMap() const
{
  if (this->sdf->HasElement("material"))
    return this->sdf->GetElement("material")->GetValueString("normal_map");
  return std::string();
}

////////////////////////////////////////////////////////////////////////////////
/// Set the normal map
void Visual::SetNormalMap(const std::string &nmap)
{
  this->sdf->GetOrCreateElement("material")->GetAttribute("normal_map")->Set(nmap);
  RTShaderSystem::Instance()->UpdateShaders();
}

////////////////////////////////////////////////////////////////////////////////
void Visual::SetRibbonTrail(bool value)
{
  if (this->ribbonTrail == NULL)
  {
    this->ribbonTrail = (Ogre::RibbonTrail*)this->sceneNode->getCreator()->createMovableObject("RibbonTrail");
    this->ribbonTrail->setMaterialName("Gazebo/Red");
    this->ribbonTrail->setTrailLength(200);
    this->ribbonTrail->setMaxChainElements(1000);
    this->ribbonTrail->setNumberOfChains(1);
    this->ribbonTrail->setVisible(false);
    this->ribbonTrail->setInitialWidth(0,0.05);
    this->sceneNode->attachObject(this->ribbonTrail);
    //this->scene->GetManager()->getRootSceneNode()->attachObject(this->ribbonTrail);
  }


  if (value)
  {
    try
    {
      this->ribbonTrail->addNode(this->sceneNode);
    } catch (...) { }
  }
  else
  {
    this->ribbonTrail->removeNode(this->sceneNode);
    this->ribbonTrail->clearChain(0);
  }
  this->ribbonTrail->setVisible(value);
}

////////////////////////////////////////////////////////////////////////////////
// Add a line to the visual
DynamicLines *Visual::CreateDynamicLine(RenderOpType type)
{
  this->preRenderConnection = event::Events::ConnectPreRenderSignal( boost::bind(&Visual::Update, this) );

  DynamicLines *line = new DynamicLines(type);
  this->lines.push_back(line);
  this->AttachObject(line);
  return line;
}

////////////////////////////////////////////////////////////////////////////////
// Delete a dynamic line
void Visual::DeleteDynamicLine(DynamicLines *line)
{
  // delete instance from lines vector
  for (std::list<DynamicLines*>::iterator iter=this->lines.begin();iter!=this->lines.end();iter++)
  {
    if (*iter == line)
    {
      this->lines.erase(iter);
      break;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Attach a vertex of a line to the position of the visual 
void Visual::AttachLineVertex( DynamicLines *_line, unsigned int _index )
{
  this->lineVertices.push_back( std::make_pair(_line, _index) );
  _line->SetPoint( _index, this->GetWorldPose().pos);
}
 
////////////////////////////////////////////////////////////////////////////////
/// Get the name of the material
std::string Visual::GetMaterialName() const
{
  return this->myMaterialName;
}

////////////////////////////////////////////////////////////////////////////////
// Get the bounds
math::Box Visual::GetBoundingBox() const
{
  math::Box box;
  this->GetBoundsHelper(this->GetSceneNode(), box);
  return box;
}

////////////////////////////////////////////////////////////////////////////////
// Get the bounding box for a scene node
void Visual::GetBoundsHelper(Ogre::SceneNode *node, math::Box &box) const
{
  node->_updateBounds();

  Ogre::SceneNode::ChildNodeIterator it = node->getChildIterator();

  for (int i=0; i < node->numAttachedObjects(); i++)
  {
    Ogre::MovableObject *obj = node->getAttachedObject(i);
    if (obj->isVisible() && obj->getMovableType() != "gazebo::ogredynamiclines")
    {
      Ogre::Any any = obj->getUserAny();
      if (any.getType() == typeid(std::string))
      {
        std::string str = Ogre::any_cast<std::string>(any);
        if (str.substr(0,3) == "rot" || str.substr(0,5) == "trans")
          continue;
      }

      Ogre::AxisAlignedBox bb = obj->getWorldBoundingBox();
      Ogre::Vector3 min = bb.getMinimum();
      Ogre::Vector3 max = bb.getMaximum();

      box.Merge(math::Box(math::Vector3(min.x, min.y, min.z), 
                          math::Vector3(max.x, max.y, max.z)));
    }
  }

  while(it.hasMoreElements())
  {
    Ogre::SceneNode *next = dynamic_cast<Ogre::SceneNode*>(it.getNext());
    this->GetBoundsHelper( next, box);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Insert a mesh into Ogre 
void Visual::InsertMesh( const common::Mesh *mesh)
{
  Ogre::MeshPtr ogreMesh;

  if (mesh->GetSubMeshCount() == 0)
  {
    gzerr << "Visual::InsertMesh no submeshes, this is an invalid mesh\n";
    return;
  }

  try
  {
    // Create a new mesh specifically for manual definition.
    ogreMesh = Ogre::MeshManager::getSingleton().createManual(mesh->GetName(),
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    for (unsigned int i=0; i < mesh->GetSubMeshCount(); i++)
    {
      Ogre::SubMesh *ogreSubMesh;
      Ogre::VertexData *vertexData;
      Ogre::VertexDeclaration* vertexDecl;
      Ogre::HardwareVertexBufferSharedPtr vBuf;
      Ogre::HardwareIndexBufferSharedPtr iBuf;
      float *vertices;
      unsigned short *indices;

      size_t currOffset = 0;

      const common::SubMesh *subMesh = mesh->GetSubMesh(i);

      ogreSubMesh = ogreMesh->createSubMesh();
      ogreSubMesh->useSharedVertices = false;
      ogreSubMesh->vertexData = new Ogre::VertexData();
      vertexData = ogreSubMesh->vertexData;
      vertexDecl = vertexData->vertexDeclaration;

      // The vertexDecl should contain positions, blending weights, normals,
      // diffiuse colors, specular colors, tex coords. In that order.
      vertexDecl->addElement(0, currOffset, Ogre::VET_FLOAT3, 
                             Ogre::VES_POSITION);
      currOffset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);

      // TODO: blending weights
      
      // normals
      if (subMesh->GetNormalCount() > 0 )
      {
        vertexDecl->addElement(0, currOffset, Ogre::VET_FLOAT3, 
                               Ogre::VES_NORMAL);
        currOffset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
      }

      // TODO: diffuse colors

      // TODO: specular colors

      // two dimensional texture coordinates
      if (subMesh->GetTexCoordCount() > 0)
      {
        vertexDecl->addElement(0, currOffset, Ogre::VET_FLOAT2,
            Ogre::VES_TEXTURE_COORDINATES, 0);
        currOffset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT2);
      }

      // allocate the vertex buffer
      vertexData->vertexCount = subMesh->GetVertexCount();

      vBuf = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(
                 vertexDecl->getVertexSize(0),
                 vertexData->vertexCount,
                 Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY,
                 false);

      vertexData->vertexBufferBinding->setBinding(0, vBuf);
      vertices = static_cast<float*>(vBuf->lock(
                      Ogre::HardwareBuffer::HBL_DISCARD));

      // allocate index buffer
      ogreSubMesh->indexData->indexCount = subMesh->GetIndexCount();

      ogreSubMesh->indexData->indexBuffer =
        Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
            Ogre::HardwareIndexBuffer::IT_16BIT,
            ogreSubMesh->indexData->indexCount,
            Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY,
            false);

      iBuf = ogreSubMesh->indexData->indexBuffer;
      indices = static_cast<unsigned short*>(
          iBuf->lock(Ogre::HardwareBuffer::HBL_DISCARD));

      unsigned int j;

      // Add all the vertices
      for (j =0; j < subMesh->GetVertexCount(); j++)
      {
        *vertices++ = subMesh->GetVertex(j).x;
        *vertices++ = subMesh->GetVertex(j).y;
        *vertices++ = subMesh->GetVertex(j).z;

        if (subMesh->GetNormalCount() > 0)
        {
          *vertices++ = subMesh->GetNormal(j).x;
          *vertices++ = subMesh->GetNormal(j).y;
          *vertices++ = subMesh->GetNormal(j).z;
        }

        if (subMesh->GetTexCoordCount() > 0)
        {
          *vertices++ = subMesh->GetTexCoord(j).x;
          *vertices++ = subMesh->GetTexCoord(j).y;
        }
      }

      // Add all the indices
      for (j =0; j < subMesh->GetIndexCount(); j++)
        *indices++ = subMesh->GetIndex(j);

      const common::Material *material;
      material = mesh->GetMaterial( subMesh->GetMaterialIndex() );
      if (material)
      {
        rendering::Material::Update( material );
        ogreSubMesh->setMaterialName( material->GetName() );
      }

      // Unlock
      vBuf->unlock();
      iBuf->unlock();
    }

    math::Vector3 max = mesh->GetMax();
    math::Vector3 min = mesh->GetMin();

    if (!max.IsFinite())
      gzthrow("Max bounding box is not finite[" << max << "]\n");

    if (!min.IsFinite())
      gzthrow("Min bounding box is not finite[" << min << "]\n");

    ogreMesh->_setBounds( Ogre::AxisAlignedBox(
          Ogre::Vector3(min.x, min.y, min.z),
          Ogre::Vector3(max.x, max.y, max.z)), 
          false );

    // this line makes clear the mesh is loaded (avoids memory leaks)
    ogreMesh->load();
  }
  catch (Ogre::Exception e)
  {
    gzerr << "Unable to create a basic Unit cylinder object" << std::endl;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Update a visual based on a message
void Visual::UpdateFromMsg( const boost::shared_ptr< msgs::Visual const> &msg )
{
  if (msg->has_is_static() && msg->is_static())
    this->MakeStatic();

  if (msg->has_pose())
    this->SetWorldPose( msgs::Convert(msg->pose()) );

  if (msg->has_scale())
    this->SetScale( msgs::Convert(msg->scale()) );

  if (msg->has_visible())
    this->SetVisible(msg->visible());

  if (msg->has_transparency())
    this->SetTransparency(msg->transparency());

  if (msg->has_material_script())
    this->SetMaterial(msg->material_script());


  /*if (msg->points.size() > 0)
  {
    DynamicLines *lines = this->AddDynamicLine(RENDERING_LINE_LIST);
    for (unsigned int i=0; i < msg->points.size(); i++)
      lines->AddPoint( msg->points[i] );
  }
  */
}

////////////////////////////////////////////////////////////////////////////////
/// Get the parent visual, if one exists
VisualPtr Visual::GetParent() const
{
  return this->parent;
}
 
bool Visual::IsPlane() const
{
  if (this->sdf->HasElement("geometry"))
  {
    sdf::ElementPtr geomElem = this->sdf->GetElement("geometry");
    return geomElem->HasElement("plane");
  }
  return false;
}

std::string Visual::GetMeshName() const
{
  if (this->sdf->HasElement("geometry"))
  {
    sdf::ElementPtr geomElem = this->sdf->GetElement("geometry");
    if (geomElem->HasElement("box"))
      return "unit_box";
    else if (geomElem->HasElement("sphere"))
      return "unit_sphere";
    else if (geomElem->HasElement("cylinder"))
      return "unit_cylinder";
    else if (geomElem->HasElement("plane"))
      return "unit_plane";
    else if (geomElem->HasElement("mesh"))
      return geomElem->GetElement("mesh")->GetValueString("filename");
  }

  return std::string();
}

